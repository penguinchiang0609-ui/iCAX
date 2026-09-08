#include "pch.h"
#include "OpenCascadeBRepBuilder.h"

#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <GC_MakeSegment.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Circle.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_Plane.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS_CompSolid.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace iCAX::OpenCascade
{
namespace
{
    using namespace iCAX::GeometryData;

    gp_Pnt ToPnt(IN const Point3& Point_)
    {
        return { Point_.X, Point_.Y, Point_.Z };
    }

    gp_Dir ToDir(IN const Direction3& Direction_)
    {
        return { Direction_.X, Direction_.Y, Direction_.Z };
    }

    gp_Ax2 ToAx2(IN const Placement3& Placement_)
    {
        return { ToPnt(Placement_.Location),
            ToDir(Placement_.ZDirection),
            ToDir(Placement_.XDirection) };
    }

    gp_Ax3 ToAx3(IN const Placement3& Placement_)
    {
        gp_Ax3 _Axis{ ToPnt(Placement_.Location),
            ToDir(Placement_.ZDirection),
            ToDir(Placement_.XDirection) };
        // Unlike gp_Ax2, surface coordinates may be left-handed. Rebuilding
        // only X/Z silently flips their UV parameterization and face normals.
        if (_Axis.YDirection().Dot(ToDir(Placement_.YDirection)) < 0) _Axis.YReverse();
        return _Axis;
    }

    TopAbs_Orientation ToOrientation(IN ETopologyOrientation Orientation_)
    {
        switch (Orientation_)
        {
        case ETopologyOrientation::Forward: return TopAbs_FORWARD;
        case ETopologyOrientation::Reversed: return TopAbs_REVERSED;
        case ETopologyOrientation::Internal: return TopAbs_INTERNAL;
        case ETopologyOrientation::External: return TopAbs_EXTERNAL;
        default: return TopAbs_FORWARD;
        }
    }

    template<class TRecord>
    const TRecord* FindRecord(
        IN const std::vector<TRecord>& Records_,
        IN std::uint64_t nID_)
    {
        const auto _Iter = std::find_if(
            Records_.begin(), Records_.end(),
            [nID_](IN const auto& Record_) { return Record_.Id == nID_; });
        return _Iter == Records_.end() ? nullptr : &*_Iter;
    }

    Handle(Geom_Curve) MakeDegreeOnePolyline(IN const std::vector<Point3>& Points_)
    {
        if (Points_.size() < 2)
        {
            return {};
        }
        NCollection_Array1<gp_Pnt> _Poles(1, static_cast<int>(Points_.size()));
        NCollection_Array1<double> _Knots(1, static_cast<int>(Points_.size()));
        NCollection_Array1<int> _Multiplicities(1, static_cast<int>(Points_.size()));
        for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
        {
            _Poles.SetValue(_Index, ToPnt(Points_[static_cast<std::size_t>(_Index - 1)]));
            _Knots.SetValue(_Index, static_cast<double>(_Index - 1));
            _Multiplicities.SetValue(
                _Index,
                (_Index == 1 || _Index == _Poles.Length()) ? 2 : 1);
        }
        return new Geom_BSplineCurve(
            _Poles, _Knots, _Multiplicities, 1, false);
    }

    Handle(Geom_Curve) MakeCurve(IN const Curve3& Curve_)
    {
        return std::visit([](IN const auto& Value_) -> Handle(Geom_Curve) {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, Line3>)
            {
                return new Geom_Line(gp_Lin(
                    ToPnt(Value_.Axis.Location),
                    ToDir(Value_.Axis.Direction)));
            }
            else if constexpr (std::is_same_v<T, Ray3>)
            {
                return new Geom_Line(gp_Lin(
                    ToPnt(Value_.Axis.Location),
                    ToDir(Value_.Axis.Direction)));
            }
            else if constexpr (std::is_same_v<T, Segment3>)
            {
                return GC_MakeSegment(ToPnt(Value_.Start), ToPnt(Value_.End)).Value();
            }
            else if constexpr (std::is_same_v<T, Circle3>)
            {
                return new Geom_Circle(ToAx2(Value_.Placement), Value_.Radius);
            }
            else if constexpr (std::is_same_v<T, Arc3>)
            {
                return new Geom_TrimmedCurve(
                    new Geom_Circle(ToAx2(Value_.Basis.Placement), Value_.Basis.Radius),
                    Value_.StartAngle,
                    Value_.EndAngle,
                    Value_.CounterClockwise);
            }
            else if constexpr (std::is_same_v<T, Ellipse3>)
            {
                return new Geom_Ellipse(
                    ToAx2(Value_.Placement), Value_.MajorRadius, Value_.MinorRadius);
            }
            else if constexpr (std::is_same_v<T, EllipseArc3>)
            {
                return new Geom_TrimmedCurve(
                    new Geom_Ellipse(
                        ToAx2(Value_.Basis.Placement),
                        Value_.Basis.MajorRadius,
                        Value_.Basis.MinorRadius),
                    Value_.StartAngle,
                    Value_.EndAngle,
                    Value_.CounterClockwise);
            }
            else if constexpr (std::is_same_v<T, Polyline3>)
            {
                return MakeDegreeOnePolyline(Value_.Points);
            }
            else if constexpr (std::is_same_v<T, Bezier3>)
            {
                if (Value_.Poles.size() < 2) return {};
                NCollection_Array1<gp_Pnt> _Poles(1, static_cast<int>(Value_.Poles.size()));
                for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
                {
                    _Poles.SetValue(_Index, ToPnt(Value_.Poles[static_cast<std::size_t>(_Index - 1)]));
                }
                return new Geom_BezierCurve(_Poles);
            }
            else if constexpr (std::is_same_v<T, BSpline3>
                || std::is_same_v<T, NURBS3>)
            {
                if (Value_.Poles.size() < 2 || Value_.Knots.empty()
                    || Value_.Multiplicities.size() != Value_.Knots.size()) return {};
                NCollection_Array1<gp_Pnt> _Poles(1, static_cast<int>(Value_.Poles.size()));
                NCollection_Array1<double> _Knots(1, static_cast<int>(Value_.Knots.size()));
                NCollection_Array1<int> _Multiplicities(1, static_cast<int>(Value_.Multiplicities.size()));
                for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
                    _Poles.SetValue(_Index, ToPnt(Value_.Poles[static_cast<std::size_t>(_Index - 1)]));
                for (int _Index = 1; _Index <= _Knots.Length(); ++_Index)
                {
                    _Knots.SetValue(_Index, Value_.Knots[static_cast<std::size_t>(_Index - 1)]);
                    _Multiplicities.SetValue(_Index, Value_.Multiplicities[static_cast<std::size_t>(_Index - 1)]);
                }
                if constexpr (std::is_same_v<T, NURBS3>)
                {
                    if (Value_.Weights.size() != Value_.Poles.size()) return {};
                    NCollection_Array1<double> _Weights(1, static_cast<int>(Value_.Weights.size()));
                    for (int _Index = 1; _Index <= _Weights.Length(); ++_Index)
                        _Weights.SetValue(_Index, Value_.Weights[static_cast<std::size_t>(_Index - 1)]);
                    return new Geom_BSplineCurve(
                        _Poles, _Weights, _Knots, _Multiplicities,
                        Value_.Degree, Value_.Periodic);
                }
                else
                {
                    return new Geom_BSplineCurve(
                        _Poles, _Knots, _Multiplicities,
                        Value_.Degree, Value_.Periodic);
                }
            }
            else
            {
                return {};
            }
        }, Curve_);
    }

    Handle(Geom2d_Curve) MakeDegreeOnePolyline2(IN const std::vector<Point2>& Points_)
    {
        if (Points_.size() < 2) return {};
        NCollection_Array1<gp_Pnt2d> _Poles(1, static_cast<int>(Points_.size()));
        NCollection_Array1<double> _Knots(1, static_cast<int>(Points_.size()));
        NCollection_Array1<int> _Multiplicities(1, static_cast<int>(Points_.size()));
        for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
        {
            const auto& _Point = Points_[static_cast<std::size_t>(_Index - 1)];
            _Poles.SetValue(_Index, { _Point.X, _Point.Y });
            _Knots.SetValue(_Index, static_cast<double>(_Index - 1));
            _Multiplicities.SetValue(
                _Index,
                (_Index == 1 || _Index == _Poles.Length()) ? 2 : 1);
        }
        return new Geom2d_BSplineCurve(
            _Poles, _Knots, _Multiplicities, 1, false);
    }

    Handle(Geom2d_Curve) MakeCurve2(IN const Curve2& Curve_)
    {
        return std::visit([](IN const auto& Value_) -> Handle(Geom2d_Curve) {
            using T = std::decay_t<decltype(Value_)>;
            const auto _Point = [](IN const Point2& P_) { return gp_Pnt2d(P_.X, P_.Y); };
            const auto _Dir = [](IN const Direction2& D_) { return gp_Dir2d(D_.X, D_.Y); };
            const auto _Ax = [&_Point, &_Dir](IN const Placement2& P_) {
                return gp_Ax22d(_Point(P_.Location), _Dir(P_.XDirection), _Dir(P_.YDirection));
            };
            if constexpr (std::is_same_v<T, Line2>)
                return new Geom2d_Line(gp_Lin2d(_Point(Value_.Axis.Location), _Dir(Value_.Axis.Direction)));
            else if constexpr (std::is_same_v<T, Ray2>)
                return new Geom2d_Line(gp_Lin2d(_Point(Value_.Axis.Location), _Dir(Value_.Axis.Direction)));
            else if constexpr (std::is_same_v<T, Segment2>)
            {
                const auto _Delta = gp_Vec2d(_Point(Value_.Start), _Point(Value_.End));
                if (_Delta.SquareMagnitude() <= 1.0e-24) return {};
                return new Geom2d_TrimmedCurve(
                    new Geom2d_Line(_Point(Value_.Start), gp_Dir2d(_Delta)),
                    0.0, _Delta.Magnitude());
            }
            else if constexpr (std::is_same_v<T, Circle2>)
                return new Geom2d_Circle(_Ax(Value_.Placement), Value_.Radius);
            else if constexpr (std::is_same_v<T, Arc2>)
                return new Geom2d_TrimmedCurve(
                    new Geom2d_Circle(_Ax(Value_.Basis.Placement), Value_.Basis.Radius),
                    Value_.StartAngle, Value_.EndAngle, Value_.CounterClockwise);
            else if constexpr (std::is_same_v<T, Ellipse2>)
                return new Geom2d_Ellipse(
                    _Ax(Value_.Placement), Value_.MajorRadius, Value_.MinorRadius);
            else if constexpr (std::is_same_v<T, EllipseArc2>)
                return new Geom2d_TrimmedCurve(
                    new Geom2d_Ellipse(
                        _Ax(Value_.Basis.Placement),
                        Value_.Basis.MajorRadius,
                        Value_.Basis.MinorRadius),
                    Value_.StartAngle, Value_.EndAngle, Value_.CounterClockwise);
            else if constexpr (std::is_same_v<T, Polyline2>)
                return MakeDegreeOnePolyline2(Value_.Points);
            else if constexpr (std::is_same_v<T, Bezier2>)
            {
                if (Value_.Poles.size() < 2) return {};
                NCollection_Array1<gp_Pnt2d> _Poles(1, static_cast<int>(Value_.Poles.size()));
                for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
                    _Poles.SetValue(_Index, _Point(Value_.Poles[static_cast<std::size_t>(_Index - 1)]));
                return new Geom2d_BezierCurve(_Poles);
            }
            else if constexpr (std::is_same_v<T, BSpline2>
                || std::is_same_v<T, NURBS2>)
            {
                if (Value_.Poles.size() < 2 || Value_.Knots.empty()
                    || Value_.Multiplicities.size() != Value_.Knots.size()) return {};
                NCollection_Array1<gp_Pnt2d> _Poles(1, static_cast<int>(Value_.Poles.size()));
                NCollection_Array1<double> _Knots(1, static_cast<int>(Value_.Knots.size()));
                NCollection_Array1<int> _Mults(1, static_cast<int>(Value_.Multiplicities.size()));
                for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
                    _Poles.SetValue(_Index, _Point(Value_.Poles[static_cast<std::size_t>(_Index - 1)]));
                for (int _Index = 1; _Index <= _Knots.Length(); ++_Index)
                {
                    _Knots.SetValue(_Index, Value_.Knots[static_cast<std::size_t>(_Index - 1)]);
                    _Mults.SetValue(_Index, Value_.Multiplicities[static_cast<std::size_t>(_Index - 1)]);
                }
                if constexpr (std::is_same_v<T, NURBS2>)
                {
                    if (Value_.Weights.size() != Value_.Poles.size()) return {};
                    NCollection_Array1<double> _Weights(1, static_cast<int>(Value_.Weights.size()));
                    for (int _Index = 1; _Index <= _Weights.Length(); ++_Index)
                        _Weights.SetValue(_Index, Value_.Weights[static_cast<std::size_t>(_Index - 1)]);
                    return new Geom2d_BSplineCurve(
                        _Poles, _Weights, _Knots, _Mults,
                        Value_.Degree, Value_.Periodic);
                }
                else return new Geom2d_BSplineCurve(
                    _Poles, _Knots, _Mults,
                    Value_.Degree, Value_.Periodic);
            }
            else return {};
        }, Curve_);
    }

    Handle(Geom_Surface) MakeSurface(IN const Surface3& Surface_)
    {
        return std::visit([](IN const auto& Value_) -> Handle(Geom_Surface) {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_same_v<T, PlaneSurface3>)
                return new Geom_Plane(ToAx3(Value_.Placement));
            else if constexpr (std::is_same_v<T, CylindricalSurface3>)
                return new Geom_CylindricalSurface(ToAx3(Value_.Placement), Value_.Radius);
            else if constexpr (std::is_same_v<T, ConicalSurface3>)
                return new Geom_ConicalSurface(ToAx3(Value_.Placement), Value_.SemiAngle, Value_.Radius);
            else if constexpr (std::is_same_v<T, SphericalSurface3>)
                return new Geom_SphericalSurface(ToAx3(Value_.Placement), Value_.Radius);
            else if constexpr (std::is_same_v<T, ToroidalSurface3>)
                return new Geom_ToroidalSurface(
                    ToAx3(Value_.Placement), Value_.MajorRadius, Value_.MinorRadius);
            else if constexpr (std::is_same_v<T, BSplineSurface3>)
            {
                if (Value_.UCount == 0 || Value_.VCount == 0
                    || Value_.Poles.size() != static_cast<std::size_t>(Value_.UCount) * Value_.VCount
                    || Value_.UKnots.empty() || Value_.VKnots.empty()) return {};
                NCollection_Array2<gp_Pnt> _Poles(1, static_cast<int>(Value_.UCount), 1, static_cast<int>(Value_.VCount));
                NCollection_Array2<double> _Weights(1, static_cast<int>(Value_.UCount), 1, static_cast<int>(Value_.VCount));
                for (int _U = 1; _U <= static_cast<int>(Value_.UCount); ++_U)
                {
                    for (int _V = 1; _V <= static_cast<int>(Value_.VCount); ++_V)
                    {
                        const auto _Offset = static_cast<std::size_t>(_U - 1) * Value_.VCount + (_V - 1);
                        _Poles.SetValue(_U, _V, ToPnt(Value_.Poles[_Offset]));
                        _Weights.SetValue(_U, _V,
                            Value_.Weights.size() == Value_.Poles.size() ? Value_.Weights[_Offset] : 1.0);
                    }
                }
                NCollection_Array1<double> _UKnots(1, static_cast<int>(Value_.UKnots.size()));
                NCollection_Array1<double> _VKnots(1, static_cast<int>(Value_.VKnots.size()));
                NCollection_Array1<int> _UMults(1, static_cast<int>(Value_.UMultiplicities.size()));
                NCollection_Array1<int> _VMults(1, static_cast<int>(Value_.VMultiplicities.size()));
                for (int _Index = 1; _Index <= _UKnots.Length(); ++_Index)
                {
                    _UKnots.SetValue(_Index, Value_.UKnots[static_cast<std::size_t>(_Index - 1)]);
                    _UMults.SetValue(_Index, Value_.UMultiplicities[static_cast<std::size_t>(_Index - 1)]);
                }
                for (int _Index = 1; _Index <= _VKnots.Length(); ++_Index)
                {
                    _VKnots.SetValue(_Index, Value_.VKnots[static_cast<std::size_t>(_Index - 1)]);
                    _VMults.SetValue(_Index, Value_.VMultiplicities[static_cast<std::size_t>(_Index - 1)]);
                }
                return new Geom_BSplineSurface(
                    _Poles, _Weights,
                    _UKnots, _VKnots, _UMults, _VMults,
                    Value_.UDegree, Value_.VDegree,
                    Value_.UPeriodic, Value_.VPeriodic);
            }
            else return {};
        }, Surface_);
    }

    TopoDS_Shape Oriented(IN const TopoDS_Shape& Shape_, IN ETopologyOrientation Orientation_)
    {
        return Shape_.Oriented(ToOrientation(Orientation_));
    }
}

SOpenCascadeBRepBuildResult BuildOpenCascadeShape(
    IN const iCAX::GeometryData::BRepModel& Geometry_)
{
    SOpenCascadeBRepBuildResult _Result;
    try
    {
        BRep_Builder _Builder;
        std::unordered_map<std::uint64_t, TopoDS_Vertex> _Vertices;
        std::unordered_map<std::uint64_t, TopoDS_Edge> _Edges;
        std::unordered_map<std::uint64_t, TopoDS_Wire> _Wires;
        std::unordered_map<std::uint64_t, TopoDS_Face> _Faces;
        std::unordered_map<std::uint64_t, TopoDS_Shell> _Shells;
        std::unordered_map<std::uint64_t, TopoDS_Solid> _Solids;
        std::unordered_map<std::uint64_t, TopoDS_CompSolid> _CompSolids;
        std::unordered_map<std::uint64_t, TopoDS_Compound> _Compounds;

        for (const auto& _Record : Geometry_.Vertices)
        {
            TopoDS_Vertex _Vertex;
            _Builder.MakeVertex(_Vertex, ToPnt(_Record.Position), _Record.Tolerance);
            _Vertices.emplace(_Record.Id, _Vertex);
        }
        for (const auto& _Record : Geometry_.Edges)
        {
            TopoDS_Edge _Edge;
            const auto* _pCurveRecord = FindRecord(Geometry_.Curves3, _Record.Curve3Id);
            const auto _Curve = _pCurveRecord ? MakeCurve(_pCurveRecord->Geometry) : Handle(Geom_Curve){};
            if (_Curve.IsNull() && !_Record.Degenerated)
                throw std::runtime_error("BRep edge has no reconstructable 3D curve: " + std::to_string(_Record.Id));
            if (_Curve.IsNull()) _Builder.MakeEdge(_Edge);
            else _Builder.MakeEdge(_Edge, _Curve, _Record.Tolerance);

            if (const auto _Start = _Vertices.find(_Record.StartVertexId); _Start != _Vertices.end())
            {
                auto _Vertex = TopoDS::Vertex(_Start->second.Oriented(TopAbs_FORWARD));
                _Builder.Add(_Edge, _Vertex);
                if (!_Curve.IsNull()) _Builder.UpdateVertex(_Vertex, _Record.Range.First, _Edge, _Record.Tolerance);
            }
            if (const auto _End = _Vertices.find(_Record.EndVertexId); _End != _Vertices.end())
            {
                auto _Vertex = TopoDS::Vertex(_End->second.Oriented(TopAbs_REVERSED));
                _Builder.Add(_Edge, _Vertex);
                if (!_Curve.IsNull()) _Builder.UpdateVertex(_Vertex, _Record.Range.Last, _Edge, _Record.Tolerance);
            }
            if (!_Curve.IsNull()) _Builder.Range(_Edge, _Record.Range.First, _Record.Range.Last);
            _Builder.SameParameter(_Edge, _Record.SameParameter);
            _Builder.SameRange(_Edge, _Record.SameRange);
            _Builder.Degenerated(_Edge, _Record.Degenerated);
            _Edge.Closed(_Record.Closed);
            _Edges.emplace(_Record.Id, _Edge);
        }
        for (const auto& _Record : Geometry_.Wires)
        {
            TopoDS_Wire _Wire;
            _Builder.MakeWire(_Wire);
            for (const auto& _Coedge : _Record.Coedges)
            {
                const auto _Edge = _Edges.find(_Coedge.EdgeId);
                if (_Edge == _Edges.end())
                    throw std::runtime_error(
                        "BRep wire " + std::to_string(_Record.Id)
                        + " references missing edge " + std::to_string(_Coedge.EdgeId));
                _Builder.Add(_Wire, Oriented(_Edge->second, _Coedge.Orientation));
            }
            _Wire.Closed(_Record.Closed);
            _Wires.emplace(_Record.Id, _Wire);
        }
        for (const auto& _Record : Geometry_.Faces)
        {
            const auto* _pSurfaceRecord = FindRecord(Geometry_.Surfaces3, _Record.Surface3Id);
            const auto _Surface = _pSurfaceRecord ? MakeSurface(_pSurfaceRecord->Geometry) : Handle(Geom_Surface){};
            if (_Surface.IsNull())
                throw std::runtime_error("BRep face has no reconstructable surface: " + std::to_string(_Record.Id));
            TopoDS_Face _Face;
            _Builder.MakeFace(_Face, _Surface, _Record.Tolerance);
            struct SPCurve
            {
                Handle(Geom2d_Curve) Curve;
                ParameterRange Range;
                ETopologyOrientation Orientation;
            };
            std::unordered_map<std::uint64_t, std::vector<SPCurve>> _PCurves;
            for (const auto _WireID : _Record.WireIds)
            {
                const auto* _pWireRecord = FindRecord(Geometry_.Wires, _WireID);
                if (!_pWireRecord) continue;
                for (const auto& _Coedge : _pWireRecord->Coedges)
                {
                    const auto* _pCurveRecord = FindRecord(
                        Geometry_.Curves2, _Coedge.Curve2Id);
                    if (!_pCurveRecord) continue;
                    auto _Curve = MakeCurve2(_pCurveRecord->Geometry);
                    if (!_Curve.IsNull())
                        _PCurves[_Coedge.EdgeId].push_back({ _Curve, _Coedge.Range, _Coedge.Orientation });
                }
            }
            for (const auto& [_EdgeID, _Curves] : _PCurves)
            {
                const auto _Edge = _Edges.find(_EdgeID);
                if (_Edge == _Edges.end() || _Curves.empty()) continue;
                if (_Curves.size() >= 2)
                {
                    // OCC selects the second seam pcurve for a reversed edge.
                    // Wire traversal order need not put the forward occurrence first.
                    const bool _ReverseOrder = _Curves[0].Orientation == ETopologyOrientation::Reversed;
                    _Builder.UpdateEdge(
                        _Edge->second,
                        _Curves[_ReverseOrder ? 1 : 0].Curve,
                        _Curves[_ReverseOrder ? 0 : 1].Curve,
                        _Face,
                        _Record.Tolerance);
                }
                else
                    _Builder.UpdateEdge(
                        _Edge->second,
                        _Curves.front().Curve,
                        _Face,
                        _Record.Tolerance);
                _Builder.Range(
                    _Edge->second,
                    _Face,
                    _Curves.front().Range.First,
                    _Curves.front().Range.Last);
            }
            for (std::size_t _Index = 0; _Index < _Record.WireIds.size(); ++_Index)
            {
                const auto _WireID = _Record.WireIds[_Index];
                const auto _Wire = _Wires.find(_WireID);
                if (_Wire == _Wires.end()) throw std::runtime_error("BRep face references missing wire");
                const auto _Orientation = _Record.WireOrientations.size()
                    == _Record.WireIds.size()
                    ? _Record.WireOrientations[_Index]
                    : ETopologyOrientation::Forward;
                _Builder.Add(_Face, Oriented(_Wire->second, _Orientation));
            }
            _Builder.NaturalRestriction(_Face, _Record.NaturalRestriction);
            // 子形状在映射表中保持 canonical forward；实际方向由拥有它的
            // Shell/Root 引用决定，避免方向被应用两次。
            _Face.Orientation(TopAbs_FORWARD);
            _Faces.emplace(_Record.Id, _Face);
        }
        for (const auto& _Record : Geometry_.Shells)
        {
            TopoDS_Shell _Shell;
            _Builder.MakeShell(_Shell);
            for (std::size_t _Index = 0; _Index < _Record.FaceIds.size(); ++_Index)
            {
                const auto _FaceID = _Record.FaceIds[_Index];
                const auto _Face = _Faces.find(_FaceID);
                if (_Face == _Faces.end()) throw std::runtime_error("BRep shell references missing face");
                const auto _Orientation = _Record.FaceOrientations.size()
                    == _Record.FaceIds.size()
                    ? _Record.FaceOrientations[_Index]
                    : FindRecord(Geometry_.Faces, _FaceID)->Orientation;
                _Builder.Add(_Shell, Oriented(_Face->second, _Orientation));
            }
            _Shell.Closed(_Record.Closed);
            _Shells.emplace(_Record.Id, _Shell);
        }
        for (const auto& _Record : Geometry_.Solids)
        {
            TopoDS_Solid _Solid;
            _Builder.MakeSolid(_Solid);
            for (std::size_t _Index = 0; _Index < _Record.ShellIds.size(); ++_Index)
            {
                const auto _ShellID = _Record.ShellIds[_Index];
                const auto _Shell = _Shells.find(_ShellID);
                if (_Shell == _Shells.end()) throw std::runtime_error("BRep solid references missing shell");
                const auto _Orientation = _Record.ShellOrientations.size()
                    == _Record.ShellIds.size()
                    ? _Record.ShellOrientations[_Index]
                    : ETopologyOrientation::Forward;
                _Builder.Add(_Solid, Oriented(_Shell->second, _Orientation));
            }
            _Solids.emplace(_Record.Id, _Solid);
        }
        for (const auto& _Record : Geometry_.CompSolids)
        {
            TopoDS_CompSolid _CompSolid;
            _Builder.MakeCompSolid(_CompSolid);
            for (const auto _SolidID : _Record.SolidIds)
            {
                const auto _Solid = _Solids.find(_SolidID);
                if (_Solid == _Solids.end()) throw std::runtime_error("BRep compsolid references missing solid");
                _Builder.Add(_CompSolid, _Solid->second);
            }
            _CompSolids.emplace(_Record.Id, _CompSolid);
        }

        std::function<TopoDS_Compound(std::uint64_t)> _BuildCompound;
        const auto _FindShape = [&](IN EBRepShapeKind Kind_, IN std::uint64_t nID_) -> TopoDS_Shape {
            switch (Kind_)
            {
            case EBRepShapeKind::Vertex: if (auto i = _Vertices.find(nID_); i != _Vertices.end()) return i->second; break;
            case EBRepShapeKind::Edge: if (auto i = _Edges.find(nID_); i != _Edges.end()) return i->second; break;
            case EBRepShapeKind::Wire: if (auto i = _Wires.find(nID_); i != _Wires.end()) return i->second; break;
            case EBRepShapeKind::Face: if (auto i = _Faces.find(nID_); i != _Faces.end()) return i->second; break;
            case EBRepShapeKind::Shell: if (auto i = _Shells.find(nID_); i != _Shells.end()) return i->second; break;
            case EBRepShapeKind::Solid: if (auto i = _Solids.find(nID_); i != _Solids.end()) return i->second; break;
            case EBRepShapeKind::CompSolid: if (auto i = _CompSolids.find(nID_); i != _CompSolids.end()) return i->second; break;
            case EBRepShapeKind::Compound: return _BuildCompound(nID_);
            }
            return {};
        };

        std::unordered_map<std::uint64_t, const BRepCompound*> _CompoundRecords;
        for (const auto& _Record : Geometry_.Compounds)
            if (!_CompoundRecords.emplace(_Record.Id, &_Record).second)
                throw std::runtime_error("BRep contains duplicate compound IDs");
        std::unordered_set<std::uint64_t> _BuildingCompounds;
        // Serialized containers need not be child-first. Build their dependency
        // graph explicitly; reject malformed cycles instead of losing children.
        _BuildCompound = [&](std::uint64_t nID_) -> TopoDS_Compound {
            if (const auto _Ready = _Compounds.find(nID_); _Ready != _Compounds.end())
                return _Ready->second;
            const auto _Found = _CompoundRecords.find(nID_);
            if (_Found == _CompoundRecords.end())
                throw std::runtime_error("BRep compound references missing compound");
            if (!_BuildingCompounds.insert(nID_).second)
                throw std::runtime_error("BRep compound references form a cycle");
            const auto& _Record = *_Found->second;
            TopoDS_Compound _Compound;
            _Builder.MakeCompound(_Compound);
            for (const auto& _Child : _Record.Children)
            {
                auto _Shape = _FindShape(_Child.Kind, _Child.Id);
                if (_Shape.IsNull()) throw std::runtime_error("BRep compound references missing child");
                _Builder.Add(_Compound, Oriented(_Shape, _Child.Orientation));
            }
            _Compounds.emplace(_Record.Id, _Compound);
            _BuildingCompounds.erase(nID_);
            return _Compound;
        };
        for (const auto& _Record : Geometry_.Compounds) _BuildCompound(_Record.Id);

        std::vector<TopoDS_Shape> _Roots;
        for (const auto& _Root : Geometry_.RootShapes)
        {
            auto _Shape = _FindShape(_Root.Kind, _Root.Id);
            if (!_Shape.IsNull()) _Roots.push_back(Oriented(_Shape, _Root.Orientation));
        }
        if (_Roots.empty())
        {
            for (const auto _ID : Geometry_.RootSolidIds)
                if (const auto i = _Solids.find(_ID); i != _Solids.end()) _Roots.push_back(i->second);
        }
        if (_Roots.empty()) throw std::runtime_error("BRep has no reconstructable root shape");
        if (_Roots.size() == 1) _Result.Shape = _Roots.front();
        else
        {
            TopoDS_Compound _Compound;
            _Builder.MakeCompound(_Compound);
            for (const auto& _Root : _Roots) _Builder.Add(_Compound, _Root);
            _Result.Shape = _Compound;
        }
        BRepLib::BuildCurves3d(_Result.Shape);
        if (!BRepCheck_Analyzer(_Result.Shape).IsValid())
            _Result.Diagnostics.push_back("Rebuilt OCC shape contains recoverable BRep validation warnings");
        _Result.bOK = !_Result.Shape.IsNull();
    }
    catch (const Standard_Failure& Error_)
    {
        _Result.Diagnostics.push_back(std::string("OCC BRep rebuild failed: ") + Error_.what());
    }
    catch (const std::exception& Error_)
    {
        _Result.Diagnostics.push_back(std::string("BRep rebuild failed: ") + Error_.what());
    }
    return _Result;
}
}
