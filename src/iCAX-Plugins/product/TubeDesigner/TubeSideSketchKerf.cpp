#include "pch.h"
#include "TubeSideSketchKerf.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepLib.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_OffsetCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2dConvert_ApproxCurve.hxx>
#include <Geom2dConvert_CompCurveToBSplineCurve.hxx>
#include <Geom_OffsetCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_Plane.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_List.hxx>
#include <NCollection_Array1.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <ShapeFix_Face.hxx>
#include <gp.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace iCAX::TubeDesigner
{
    namespace
    {
        std::vector<TopoDS_Wire> Wires(const TopoDS_Shape& Shape_)
        {
            std::vector<TopoDS_Wire> _Result;
            for (TopExp_Explorer _Explorer(Shape_, TopAbs_WIRE); _Explorer.More(); _Explorer.Next())
                _Result.push_back(TopoDS::Wire(_Explorer.Current()));
            return _Result;
        }

        occ::handle<Geom2d_Curve> WorldXYCurve(
            const occ::handle<Geom_Curve>& Curve_, double Parameter_)
        {
            if(const auto _Trimmed = occ::down_cast<Geom_TrimmedCurve>(Curve_); !_Trimmed.IsNull())
                return WorldXYCurve(_Trimmed->BasisCurve(), Parameter_);
            if(const auto _Offset = occ::down_cast<Geom_OffsetCurve>(Curve_); !_Offset.IsNull())
            {
                const auto _Basis = WorldXYCurve(_Offset->BasisCurve(), Parameter_);
                if(_Basis.IsNull()) throw std::runtime_error("偏置曲线缺少平面基线");
                const occ::handle<Geom2d_Curve> _Positive = new Geom2d_OffsetCurve(_Basis, _Offset->Offset());
                const auto _Target = Curve_->Value(Parameter_);
                const auto _P = _Positive->Value(Parameter_);
                const auto _N = Geom2d_OffsetCurve(_Basis, -_Offset->Offset()).Value(Parameter_);
                return gp_Pnt2d(_Target.X(),_Target.Y()).Distance(_P)
                    <= gp_Pnt2d(_Target.X(),_Target.Y()).Distance(_N)
                    ? _Positive
                    : occ::handle<Geom2d_Curve>(new Geom2d_OffsetCurve(_Basis, -_Offset->Offset()));
            }
            return GeomAPI::To2d(Curve_,gp_Pln(gp::Origin(),gp::DZ()));
        }

        TopoDS_Shape PersistentOffsetWires(
            const TopoDS_Shape& Shape_,double Tolerance_,bool& Approximate_)
        {
            if(Shape_.IsNull()) return {};
            BRep_Builder _Builder;
            TopoDS_Compound _Result;
            _Builder.MakeCompound(_Result);
            const auto _Tolerance = std::min(Tolerance_, 1.0e-8);
            for(const auto& _SourceWire : Wires(Shape_))
            {
                BRepBuilderAPI_MakeWire _Wire;
                for(BRepTools_WireExplorer _It(_SourceWire); _It.More(); _It.Next())
                {
                    auto _Edge = _It.Current();
                    const auto _Type = BRepAdaptor_Curve(_Edge).GetType();
                    if(_Type == GeomAbs_OffsetCurve || _Type == GeomAbs_OtherCurve)
                    {
                        double _First, _Last;
                        const auto _Source3d = BRep_Tool::Curve(_Edge,_First,_Last);
                        const auto _Source2d = WorldXYCurve(_Source3d,(_First+_Last)*0.5);
                        if(_Source2d.IsNull()) throw std::runtime_error("偏置曲线不能转换到展开平面");
                        const occ::handle<Geom2d_Curve> _Trimmed = new Geom2d_TrimmedCurve(_Source2d,_First,_Last);
                        Geom2dConvert_ApproxCurve _Approx(_Trimmed,_Tolerance,GeomAbs_C1,2048,8);
                        if(!_Approx.IsDone() || _Approx.Curve().IsNull() || _Approx.MaxError()>_Tolerance)
                            throw std::runtime_error("完整偏置曲线无法满足平滑样条公差");
                        const auto _Curve = _Approx.Curve();
                        const auto _NewFirst = _Curve->FirstParameter(), _NewLast = _Curve->LastParameter();
                        NCollection_Array1<double> _Knots(1,_Curve->NbKnots());
                        for(int _I=1;_I<=_Curve->NbKnots();++_I)
                            _Knots.SetValue(_I,_First+(_Curve->Knot(_I)-_NewFirst)*(_Last-_First)/(_NewLast-_NewFirst));
                        _Curve->SetKnots(_Knots);
                        const auto _Curve3d = GeomAPI::To3d(_Curve,gp_Pln(gp::Origin(),gp::DZ()));
                        if(_Curve3d.IsNull()) throw std::runtime_error("偏置样条缺少同参数空间曲线");
                        auto _Replacement = BRepBuilderAPI_MakeEdge(_Curve3d,_First,_Last).Edge();
                        _Replacement.Orientation(_Edge.Orientation());
                        _Edge = _Replacement;
                        Approximate_ = true;
                    }
                    _Wire.Add(_Edge);
                    if(!_Wire.IsDone()) throw std::runtime_error("平滑偏置样条端点不能连接");
                }
                if(!_Wire.IsDone()) throw std::runtime_error("完整偏置轮廓无效");
                _Builder.Add(_Result,_Wire.Wire());
            }
            return _Result;
        }

        TopoDS_Shape Offset(const TopoDS_Wire& Wire_, double Distance_, bool Open_,
            double Tolerance_, bool& Approximate_)
        {
            std::vector<TopoDS_Edge> _Edges;
            BRepBuilderAPI_MakeWire _RegularWire;
            for (BRepTools_WireExplorer _Explorer(Wire_); _Explorer.More(); _Explorer.Next())
            {
                auto _Edge = _Explorer.Current();
                const BRepAdaptor_Curve _Curve(_Edge);
                if(_Curve.GetType()==GeomAbs_BezierCurve)
                {
                    const auto _Bezier=_Curve.Bezier();
                    const auto _Start=_Curve.Value(_Curve.FirstParameter()), _End=_Curve.Value(_Curve.LastParameter());
                    const gp_Vec _Direction(_Start,_End);
                    const auto _Length2=_Direction.SquareMagnitude();
                    bool _Monotonic=_Length2>1.0e-20;
                    double _Previous=0.0;
                    // A collinear monotone Bezier is exactly the same finite
                    // line segment, even if repeated endpoint poles give it a
                    // zero parameter speed. Remove that parameter singularity
                    // only in the temporary kerf spine; the saved curve stays
                    // editable in its original Bezier/spline representation.
                    for(int _I=1;_Monotonic && _I<=_Bezier->NbPoles();++_I)
                    {
                        const gp_Vec _Delta(_Start,_Bezier->Pole(_I));
                        const auto _Along=_Delta.Dot(_Direction)/_Length2;
                        const auto _Across=_Delta.Crossed(_Direction).Magnitude()/std::sqrt(_Length2);
                        _Monotonic=_Across<=1.0e-10 && _Along>=_Previous-1.0e-12 && _Along<=1.0+1.0e-12;
                        _Previous=_Along;
                    }
                    if(_Monotonic)
                    {
                        auto _Replacement=BRepBuilderAPI_MakeEdge(_Start,_End).Edge();
                        _Replacement.Orientation(_Edge.Orientation());
                        _Edge=_Replacement;
                    }
                }
                _Edges.push_back(_Edge);
                _RegularWire.Add(_Edge);
                if(!_RegularWire.IsDone()) throw std::runtime_error("切缝基线的原生曲线端点不能连接");
            }
            const gp_Pln _XY(gp::Origin(), gp::DZ());
            if(_Edges.size()>1 && std::any_of(_Edges.begin(),_Edges.end(),[](const auto& Edge){
                    const auto _Type=BRepAdaptor_Curve(Edge).GetType();
                    return _Type==GeomAbs_BezierCurve || _Type==GeomAbs_BSplineCurve;
                }) && std::all_of(_Edges.begin(),_Edges.end(),[](const auto& Edge){
                    const auto _Type=BRepAdaptor_Curve(Edge).GetType();
                    return _Type==GeomAbs_Line || _Type==GeomAbs_BezierCurve || _Type==GeomAbs_BSplineCurve;
                }))
            {
                // Bezier span boundaries belong to the curve representation,
                // not to its geometry. OCCT's wire intersection offset may
                // discard one side at a perfectly tangent span junction. Join
                // smooth spans exactly as one reparameterized BSpline before
                // calculating its complete mathematical parallel instead.
                Geom2dConvert_CompCurveToBSplineCurve _Joined;
                bool _Connected=true;
                for(const auto& _Edge : _Edges)
                {
                    double _First,_Last;
                    const auto _Source3d=BRep_Tool::Curve(_Edge,_First,_Last);
                    const auto _Source=GeomAPI::To2d(_Source3d,_XY);
                    const occ::handle<Geom2d_BoundedCurve> _Trimmed=new Geom2d_TrimmedCurve(_Source,_First,_Last);
                    if(_Edge.Orientation()==TopAbs_REVERSED) _Trimmed->Reverse();
                    if(!_Joined.Add(_Trimmed,1.0e-10,true)) {_Connected=false;break;}
                }
                const auto _Curve=_Joined.BSplineCurve();
                if(_Connected && !_Curve.IsNull() && _Curve->IsCN(1))
                {
                    const auto _Curve3d=GeomAPI::To3d(_Curve,_XY);
                    const auto _Edge=BRepBuilderAPI_MakeEdge(_Curve3d).Edge();
                    return Offset(BRepBuilderAPI_MakeWire(_Edge).Wire(),Distance_,Open_,Tolerance_,Approximate_);
                }
            }
            if (_Edges.size() == 1)
            {
                double _First, _Last;
                const auto _Source3d = BRep_Tool::Curve(_Edges.front(), _First, _Last);
                const auto _Source = GeomAPI::To2d(_Source3d, _XY);
                occ::handle<Geom2d_Curve> _Parallel;
                Geom2dAdaptor_Curve _Adaptor(_Source, _First, _Last);
                if (_Adaptor.GetType() == GeomAbs_Line)
                {
                    auto _Line = _Adaptor.Line();
                    _Line.SetLocation(_Line.Location().Translated(gp_Vec2d(
                        _Line.Direction().Y(), -_Line.Direction().X()) * Distance_));
                    _Parallel = new Geom2d_Line(_Line);
                }
                else if (_Adaptor.GetType() == GeomAbs_Circle)
                {
                    const auto _Circle = _Adaptor.Circle();
                    gp_Pnt2d _Point;
                    gp_Vec2d _Tangent;
                    _Source->D1(_First, _Point, _Tangent);
                    const auto _Sign = gp_Vec2d(_Tangent.Y(), -_Tangent.X()).Dot(
                        gp_Vec2d(_Circle.Location(), _Point)) >= 0.0 ? 1.0 : -1.0;
                    const auto _Radius = _Circle.Radius() + Distance_ * _Sign;
                    if (_Radius <= 1.0e-10) return {};
                    _Parallel = new Geom2d_Circle(_Circle.Position(), _Radius);
                }
                else
                {
                    const occ::handle<Geom2d_Curve> _Trimmed = new Geom2d_TrimmedCurve(_Source, _First, _Last);
                    _Parallel = new Geom2d_OffsetCurve(_Trimmed, Distance_);
                }
                const occ::handle<Geom_Surface> _Plane = new Geom_Plane(_XY);
                auto _Edge = BRepBuilderAPI_MakeEdge(_Parallel, _Plane, _First, _Last).Edge();
                _Edge.Orientation(_Edges.front().Orientation());
                if (!BRepLib::BuildCurve3d(_Edge, 1.0e-7))
                    throw std::runtime_error("原生曲线的完整平行偏置生成失败");
                return PersistentOffsetWires(BRepBuilderAPI_MakeWire(_Edge).Wire(),Tolerance_,Approximate_);
            }
            const auto _Spine = BRepBuilderAPI_MakeFace(_XY, _RegularWire.Wire(), false).Face();
            BRepOffsetAPI_MakeOffset _Offset(_Spine, GeomAbs_Intersection, Open_);
            _Offset.SetApprox(false);
            _Offset.Perform(Distance_);
            return _Offset.IsDone() ? PersistentOffsetWires(_Offset.Shape(),Tolerance_,Approximate_) : TopoDS_Shape{};
        }

        std::pair<gp_Pnt, gp_Pnt> Ends(const TopoDS_Wire& Wire_)
        {
            TopoDS_Vertex _First, _Last;
            TopExp::Vertices(Wire_, _First, _Last);
            if (_First.IsNull() || _Last.IsNull()) throw std::runtime_error("开放切缝缺少端点");
            return {BRep_Tool::Pnt(_First), BRep_Tool::Pnt(_Last)};
        }

        void AppendOrdered(BRepBuilderAPI_MakeWire& Boundary_, const TopoDS_Wire& Wire_)
        {
            // A reversed wire retains its storage order. Add(Wire) can start
            // at its disconnected far end and stop before adding every edge.
            for(BRepTools_WireExplorer _It(Wire_); _It.More(); _It.Next())
            {
                Boundary_.Add(_It.Current());
                if(!Boundary_.IsDone())
                    throw std::runtime_error("完整切缝偏置边界未按端点连接");
            }
        }

        TopoDS_Shape Faces(const TopoDS_Shape& Offset_, double& Area_)
        {
            BRep_Builder _Builder;
            TopoDS_Compound _Faces;
            _Builder.MakeCompound(_Faces);
            Area_ = 0.0;
            for (const auto& _Wire : Wires(Offset_))
            {
                BRepBuilderAPI_MakeFace _Face(gp_Pln(gp::Origin(), gp::DZ()), _Wire, true);
                if (!_Face.IsDone() || !BRepCheck_Analyzer(_Face.Face()).IsValid())
                    throw std::runtime_error("闭合切缝偏置未形成有效曲线区域");
                GProp_GProps _Properties;
                BRepGProp::SurfaceProperties(_Face.Face(), _Properties);
                Area_ += std::abs(_Properties.Mass());
                _Builder.Add(_Faces, _Face.Face());
            }
            return _Faces;
        }
    }

    TopoDS_Shape BuildTubeSideSketchKerf(
        const TopoDS_Wire& Wire_, double Width_, double Tolerance_, bool* Approximate_)
    {
        if (Wire_.IsNull() || !std::isfinite(Width_) || Width_ <= 0.0
            || !std::isfinite(Tolerance_) || Tolerance_ <= 0.0)
            throw std::invalid_argument("开放切缝宽度或曲线无效");
        TopoDS_Vertex _First, _Last;
        TopExp::Vertices(Wire_, _First, _Last);
        const auto _Closed = Wire_.Closed() || (!_First.IsNull() && !_Last.IsNull()
            && BRep_Tool::Pnt(_First).Distance(BRep_Tool::Pnt(_Last)) <= Tolerance_);
        bool _Approximate = false;
        const auto _A = Offset(Wire_, Width_ * 0.5, !_Closed,Tolerance_,_Approximate);
        const auto _B = Offset(Wire_, -Width_ * 0.5, !_Closed,Tolerance_,_Approximate);
        if(Approximate_) *Approximate_ = _Approximate;
        if (_Closed)
        {
            double _AreaA, _AreaB;
            const auto _FacesA = Faces(_A, _AreaA);
            const auto _FacesB = Faces(_B, _AreaB);
            if (_AreaA <= Tolerance_ * Tolerance_ && _AreaB <= Tolerance_ * Tolerance_)
                throw std::runtime_error("闭合切缝未形成有效偏置区域");
            const auto _Outer = _AreaA >= _AreaB ? _FacesA : _FacesB;
            const auto _Inner = _AreaA >= _AreaB ? _FacesB : _FacesA;
            if (std::min(_AreaA, _AreaB) <= Tolerance_ * Tolerance_) return _Outer;
            NCollection_List<TopoDS_Shape> _Arguments, _Tools;
            _Arguments.Append(_Outer);
            _Tools.Append(_Inner);
            BRepAlgoAPI_Cut _Band;
            _Band.SetArguments(_Arguments);
            _Band.SetTools(_Tools);
            _Band.SetNonDestructive(true);
            _Band.SetFuzzyValue(Tolerance_);
            _Band.Build();
            if (!_Band.IsDone() || _Band.Shape().IsNull() || !BRepCheck_Analyzer(_Band.Shape()).IsValid())
                throw std::runtime_error("完整闭合曲线的有限宽切缝构造失败");
            return _Band.Shape();
        }
        const auto _WiresA = Wires(_A);
        const auto _WiresB = Wires(_B);
        if (_WiresA.size() != 1 || _WiresB.size() != 1)
            throw std::runtime_error("开放曲线的完整偏置发生分叉或自交（左右轮廓数 "
                + std::to_string(_WiresA.size()) + "/" + std::to_string(_WiresB.size()) + "）");
        auto _Left = _WiresA.front();
        auto _Right = _WiresB.front();
        const auto _SourceStart = Ends(Wire_).first;
        if (Ends(_Left).second.Distance(_SourceStart) < Ends(_Left).first.Distance(_SourceStart)) _Left.Reverse();
        if (Ends(_Right).second.Distance(_SourceStart) < Ends(_Right).first.Distance(_SourceStart)) _Right.Reverse();
        const auto _LeftEnds = Ends(_Left);
        const auto _RightEnds = Ends(_Right);
        BRepBuilderAPI_MakeWire _Boundary;
        AppendOrdered(_Boundary, _Left);
        _Boundary.Add(BRepBuilderAPI_MakeEdge(_LeftEnds.second, _RightEnds.second).Edge());
        _Right.Reverse();
        AppendOrdered(_Boundary, _Right);
        _Boundary.Add(BRepBuilderAPI_MakeEdge(_RightEnds.first, _LeftEnds.first).Edge());
        if (!_Boundary.IsDone()) throw std::runtime_error("开放曲线的偏置和端盖不能连成完整轮廓");
        BRepBuilderAPI_MakeFace _Region(gp_Pln(gp::Origin(), gp::DZ()), _Boundary.Wire(), true);
        if (!_Region.IsDone()) throw std::runtime_error("完整开放曲线的有限宽切缝区域未形成面");
        auto _Face = _Region.Face();
        BRepLib::SameParameter(_Face, Tolerance_, true);
        if (!BRepCheck_Analyzer(_Face).IsValid())
        {
            ShapeFix_Face _Fix(_Face);
            _Fix.SetPrecision(Tolerance_);
            _Fix.SetMaxTolerance(std::max(1.0e-6, Tolerance_ * 10.0));
            _Fix.Perform();
            _Face = _Fix.Face();
        }
        BRepCheck_Analyzer _Check(_Face);
        if (!_Check.IsValid())
        {
            std::ostringstream _Details;
            _Details << "完整开放曲线的有限宽切缝区域无效";
            for(TopExp_Explorer _It(_Face, TopAbs_EDGE); _It.More(); _It.Next())
                for(const auto _Status : _Check.Result(_It.Current())->Status())
                    if(_Status != BRepCheck_NoError) _Details << " edge=" << static_cast<int>(_Status);
            for(const auto _Status : _Check.Result(_Face)->Status())
                if(_Status != BRepCheck_NoError) _Details << " face=" << static_cast<int>(_Status);
            throw std::runtime_error(_Details.str());
        }
        return _Face;
    }
}
