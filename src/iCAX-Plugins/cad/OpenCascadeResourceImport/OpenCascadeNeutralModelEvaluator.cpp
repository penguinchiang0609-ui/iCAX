#include "pch.h"

#include "OpenCascadeNeutralModelEvaluator.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Ellipse.hxx>
#include <ShapeFix_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <NCollection_List.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using iCAX::TemplateRuntime::EGeometryOperator;
    using iCAX::TemplateRuntime::SGeometryNode;
    using iCAX::TemplateRuntime::SNeutralModel;

    constexpr double kTolerance = 1.0e-9;

    const Variant* Find(const ObjectMap& Object_, const std::string& strName_)
    {
        const auto _Iterator = Object_.find(strName_);
        return _Iterator == Object_.end() ? nullptr : &_Iterator->second;
    }

    const Variant& Require(const ObjectMap& Object_, const std::string& strName_, const std::string& strPath_)
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value) throw std::invalid_argument(strPath_ + "." + strName_ + " is required");
        return *_Value;
    }

    const ObjectMap& RequireObject(const Variant& Value_, const std::string& strPath_)
    {
        if (!Value_.Is<ObjectMap>()) throw std::invalid_argument(strPath_ + " must be an object");
        return std::get<ObjectMap>(Value_.m_Value);
    }

    const VariantArray& RequireArray(const Variant& Value_, const std::string& strPath_)
    {
        if (!Value_.Is<VariantArray>()) throw std::invalid_argument(strPath_ + " must be an array");
        return std::get<VariantArray>(Value_.m_Value);
    }

    std::string RequireString(const ObjectMap& Object_, const std::string& strName_, const std::string& strPath_)
    {
        const auto& _Value = Require(Object_, strName_, strPath_);
        if (!_Value.Is<std::string>() || _Value.To<std::string>().empty())
            throw std::invalid_argument(strPath_ + "." + strName_ + " must be a non-empty string");
        return _Value.To<std::string>();
    }

    double Number(const Variant& Value_, const std::string& strPath_)
    {
        double _Result = 0.0;
        if (Value_.Is<double>()) _Result = Value_.To<double>();
        else if (Value_.Is<float>()) _Result = Value_.To<float>();
        else if (Value_.Is<int>()) _Result = Value_.To<int>();
        else if (Value_.Is<unsigned int>()) _Result = Value_.To<unsigned int>();
        else if (Value_.Is<long long>()) _Result = static_cast<double>(Value_.To<long long>());
        else if (Value_.Is<unsigned long long>()) _Result = static_cast<double>(Value_.To<unsigned long long>());
        else throw std::invalid_argument(strPath_ + " must be numeric");
        if (!std::isfinite(_Result)) throw std::invalid_argument(strPath_ + " must be finite");
        return _Result;
    }

    double OptionalNumber(const ObjectMap& Object_, const std::string& strName_, double dDefault_, const std::string& strPath_)
    {
        const auto _Value = Find(Object_, strName_);
        return _Value ? Number(*_Value, strPath_ + "." + strName_) : dDefault_;
    }

    int Integer(const Variant& Value_, const std::string& strPath_)
    {
        const auto _Number = Number(Value_, strPath_);
        if (std::trunc(_Number) != _Number
            || _Number < static_cast<double>(std::numeric_limits<int>::min())
            || _Number > static_cast<double>(std::numeric_limits<int>::max()))
        {
            throw std::invalid_argument(strPath_ + " must be an integer");
        }
        return static_cast<int>(_Number);
    }

    bool OptionalBoolean(
        const ObjectMap& Object_, const std::string& strName_, bool bDefault_,
        const std::string& strPath_)
    {
        const auto _Value = Find(Object_, strName_);
        if (!_Value) return bDefault_;
        if (!_Value->Is<bool>())
            throw std::invalid_argument(strPath_ + "." + strName_ + " must be boolean");
        return _Value->To<bool>();
    }

    gp_Vec Vector3(const Variant& Value_, const std::string& strPath_)
    {
        const auto& _Array = RequireArray(Value_, strPath_);
        if (_Array.size() != 3) throw std::invalid_argument(strPath_ + " must contain three numbers");
        return gp_Vec(
            Number(_Array[0], strPath_ + "[0]"),
            Number(_Array[1], strPath_ + "[1]"),
            Number(_Array[2], strPath_ + "[2]"));
    }

    gp_Pnt Point3(const Variant& Value_, const std::string& strPath_)
    {
        const auto _Vector = Vector3(Value_, strPath_);
        return gp_Pnt(_Vector.X(), _Vector.Y(), _Vector.Z());
    }

    struct SPlacement final
    {
        gp_Pnt Origin;
        gp_Dir XAxis;
        gp_Dir YAxis;
    };

    SPlacement Placement(const ObjectMap& Arguments_, const std::string& strPath_)
    {
        const auto& _Placement = RequireObject(
            Require(Arguments_, "placement", strPath_), strPath_ + ".placement");
        const auto _Origin = Point3(Require(_Placement, "origin", strPath_ + ".placement"), strPath_ + ".placement.origin");
        const auto _X = Vector3(Require(_Placement, "xAxis", strPath_ + ".placement"), strPath_ + ".placement.xAxis");
        const auto _Y = Vector3(Require(_Placement, "yAxis", strPath_ + ".placement"), strPath_ + ".placement.yAxis");
        if (_X.Magnitude() <= kTolerance || _Y.Magnitude() <= kTolerance
            || _X.Crossed(_Y).Magnitude() <= kTolerance)
        {
            throw std::invalid_argument(strPath_ + ".placement axes must define a plane");
        }
        const gp_Dir _XAxis(_X);
        const gp_Dir _Normal(_X.Crossed(_Y));
        const gp_Dir _YAxis(gp_Vec(_Normal).Crossed(gp_Vec(_XAxis)));
        return { _Origin, _XAxis, _YAxis };
    }

    gp_Pnt LocalPoint(const SPlacement& Placement_, double dX_, double dY_)
    {
        return Placement_.Origin.Translated(
            gp_Vec(Placement_.XAxis) * dX_ + gp_Vec(Placement_.YAxis) * dY_);
    }

    std::pair<double, double> Point2(const Variant& Value_, const std::string& strPath_)
    {
        const auto& _Array = RequireArray(Value_, strPath_);
        if (_Array.size() != 2)
            throw std::invalid_argument(strPath_ + " must contain two numbers");
        return {
            Number(_Array[0], strPath_ + "[0]"),
            Number(_Array[1], strPath_ + "[1]")
        };
    }

    TopoDS_Edge Line(const gp_Pnt& Start_, const gp_Pnt& End_, const std::string& strPath_)
    {
        BRepBuilderAPI_MakeEdge _Builder(Start_, End_);
        if (!_Builder.IsDone()) throw std::runtime_error(strPath_ + " failed to build a line");
        return _Builder.Edge();
    }

    TopoDS_Edge Arc(const gp_Pnt& Start_, const gp_Pnt& Middle_, const gp_Pnt& End_, const std::string& strPath_)
    {
        GC_MakeArcOfCircle _Curve(Start_, Middle_, End_);
        if (!_Curve.IsDone()) throw std::runtime_error(strPath_ + " failed to build an arc");
        BRepBuilderAPI_MakeEdge _Builder(_Curve.Value());
        if (!_Builder.IsDone()) throw std::runtime_error(strPath_ + " failed to build an arc edge");
        return _Builder.Edge();
    }

    std::vector<std::pair<double, double>> Point2List(
        const Variant& Value_, const std::string& strPath_, std::size_t nMinimum_ = 1)
    {
        const auto& _Values = RequireArray(Value_, strPath_);
        if (_Values.size() < nMinimum_ || _Values.size() > 10000)
            throw std::invalid_argument(strPath_ + " has an invalid point count");
        std::vector<std::pair<double, double>> _Result;
        _Result.reserve(_Values.size());
        for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
            _Result.push_back(Point2(_Values[_Index], strPath_ + "[" + std::to_string(_Index) + "]"));
        return _Result;
    }

    std::vector<double> NumberList(
        const Variant& Value_, const std::string& strPath_, std::size_t nMinimum_ = 1)
    {
        const auto& _Values = RequireArray(Value_, strPath_);
        if (_Values.size() < nMinimum_ || _Values.size() > 10000)
            throw std::invalid_argument(strPath_ + " has an invalid value count");
        std::vector<double> _Result;
        _Result.reserve(_Values.size());
        for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
            _Result.push_back(Number(_Values[_Index], strPath_ + "[" + std::to_string(_Index) + "]"));
        return _Result;
    }

    std::vector<int> IntegerList(
        const Variant& Value_, const std::string& strPath_, std::size_t nMinimum_ = 1)
    {
        const auto& _Values = RequireArray(Value_, strPath_);
        if (_Values.size() < nMinimum_ || _Values.size() > 10000)
            throw std::invalid_argument(strPath_ + " has an invalid value count");
        std::vector<int> _Result;
        _Result.reserve(_Values.size());
        for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
            _Result.push_back(Integer(_Values[_Index], strPath_ + "[" + std::to_string(_Index) + "]"));
        return _Result;
    }

    TopoDS_Edge MakeSplineEdge(
        const SPlacement& Placement_, const ObjectMap& Segment_, bool bRational_,
        const std::string& strPath_)
    {
        const auto _Degree = Integer(Require(Segment_, "degree", strPath_), strPath_ + ".degree");
        if (_Degree < 1 || _Degree > Geom_BSplineCurve::MaxDegree())
            throw std::invalid_argument(strPath_ + ".degree is unsupported");
        const auto _Points = Point2List(
            Require(Segment_, "controlPoints", strPath_), strPath_ + ".controlPoints", 2);
        const auto _Periodic = OptionalBoolean(Segment_, "periodic", false, strPath_);
        auto _RawKnots = NumberList(Require(Segment_, "knots", strPath_), strPath_ + ".knots", 2);
        std::vector<double> _Knots;
        std::vector<int> _Multiplicities;
        if (const auto _Values = Find(Segment_, "multiplicities"))
        {
            _Knots = std::move(_RawKnots);
            _Multiplicities = IntegerList(*_Values, strPath_ + ".multiplicities", 2);
            if (_Multiplicities.size() != _Knots.size())
                throw std::invalid_argument(strPath_ + ".multiplicities must match knots");
        }
        else
        {
            for (const auto _Knot : _RawKnots)
            {
                if (!_Knots.empty() && _Knot < _Knots.back())
                    throw std::invalid_argument(strPath_ + ".knots must be non-decreasing");
                if (!_Knots.empty() && _Knot == _Knots.back())
                    ++_Multiplicities.back();
                else
                {
                    _Knots.push_back(_Knot);
                    _Multiplicities.push_back(1);
                }
            }
        }
        if (_Knots.size() < 2)
            throw std::invalid_argument(strPath_ + ".knots requires at least two unique values");
        for (std::size_t _Index = 0; _Index < _Knots.size(); ++_Index)
        {
            if (_Index > 0 && _Knots[_Index] <= _Knots[_Index - 1])
                throw std::invalid_argument(strPath_ + ".knots must be strictly increasing");
            const auto _Maximum = (!_Periodic && (_Index == 0 || _Index + 1 == _Knots.size()))
                ? _Degree + 1 : _Degree;
            if (_Multiplicities[_Index] < 1 || _Multiplicities[_Index] > _Maximum)
                throw std::invalid_argument(strPath_ + ".multiplicities is invalid for degree");
        }
        const auto _MultiplicitySum = std::accumulate(
            _Multiplicities.begin(), _Multiplicities.end(), 0ll);
        if (!_Periodic
            && _MultiplicitySum - _Degree - 1 != static_cast<long long>(_Points.size()))
        {
            throw std::invalid_argument(
                strPath_ + " control point, knot, multiplicity, and degree counts disagree");
        }
        if (_Periodic && _Multiplicities.front() != _Multiplicities.back())
            throw std::invalid_argument(strPath_ + " periodic end multiplicities must match");
        if (_Periodic
            && _MultiplicitySum - _Multiplicities.back()
                != static_cast<long long>(_Points.size()))
        {
            throw std::invalid_argument(
                strPath_ + " periodic control point, knot, and multiplicity counts disagree");
        }

        NCollection_Array1<gp_Pnt> _Poles(1, static_cast<int>(_Points.size()));
        NCollection_Array1<double> _KnotArray(1, static_cast<int>(_Knots.size()));
        NCollection_Array1<int> _MultiplicityArray(1, static_cast<int>(_Multiplicities.size()));
        for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
        {
            const auto& [_X, _Y] = _Points[static_cast<std::size_t>(_Index - 1)];
            _Poles.SetValue(_Index, LocalPoint(Placement_, _X, _Y));
        }
        for (int _Index = 1; _Index <= _KnotArray.Length(); ++_Index)
        {
            _KnotArray.SetValue(_Index, _Knots[static_cast<std::size_t>(_Index - 1)]);
            _MultiplicityArray.SetValue(
                _Index, _Multiplicities[static_cast<std::size_t>(_Index - 1)]);
        }

        Handle(Geom_BSplineCurve) _Curve;
        if (bRational_)
        {
            const auto _Weights = NumberList(
                Require(Segment_, "weights", strPath_), strPath_ + ".weights", 2);
            if (_Weights.size() != _Points.size())
                throw std::invalid_argument(strPath_ + ".weights must match controlPoints");
            NCollection_Array1<double> _WeightArray(1, static_cast<int>(_Weights.size()));
            for (int _Index = 1; _Index <= _WeightArray.Length(); ++_Index)
            {
                const auto _Weight = _Weights[static_cast<std::size_t>(_Index - 1)];
                if (_Weight <= 0.0)
                    throw std::invalid_argument(strPath_ + ".weights must be positive");
                _WeightArray.SetValue(_Index, _Weight);
            }
            _Curve = new Geom_BSplineCurve(
                _Poles, _WeightArray, _KnotArray, _MultiplicityArray, _Degree, _Periodic);
        }
        else
        {
            if (Find(Segment_, "weights"))
                throw std::invalid_argument(strPath_ + ".weights is only valid for nurbs");
            _Curve = new Geom_BSplineCurve(
                _Poles, _KnotArray, _MultiplicityArray, _Degree, _Periodic);
        }

        const auto _StartParameter = Find(Segment_, "startParameter");
        const auto _EndParameter = Find(Segment_, "endParameter");
        if (static_cast<bool>(_StartParameter) != static_cast<bool>(_EndParameter))
            throw std::invalid_argument(
                strPath_ + ".startParameter and endParameter must be supplied together");
        if (_StartParameter)
        {
            const auto _Start = Number(*_StartParameter, strPath_ + ".startParameter");
            const auto _End = Number(*_EndParameter, strPath_ + ".endParameter");
            if (_End - _Start <= kTolerance)
                throw std::invalid_argument(strPath_ + " parameter range must increase");
            BRepBuilderAPI_MakeEdge _Builder(_Curve, _Start, _End);
            if (!_Builder.IsDone()) throw std::runtime_error(strPath_ + " failed to build a spline edge");
            return _Builder.Edge();
        }
        BRepBuilderAPI_MakeEdge _Builder(_Curve);
        if (!_Builder.IsDone()) throw std::runtime_error(strPath_ + " failed to build a spline edge");
        return _Builder.Edge();
    }

    TopoDS_Edge MakePathEdge(
        const SPlacement& Placement_, const ObjectMap& Segment_, const std::string& strPath_)
    {
        const auto _Kind = RequireString(Segment_, "kind", strPath_);
        if (_Kind == "line")
        {
            const auto [_StartX, _StartY] = Point2(
                Require(Segment_, "start", strPath_), strPath_ + ".start");
            const auto [_EndX, _EndY] = Point2(
                Require(Segment_, "end", strPath_), strPath_ + ".end");
            return Line(
                LocalPoint(Placement_, _StartX, _StartY),
                LocalPoint(Placement_, _EndX, _EndY), strPath_);
        }
        if (_Kind == "arc")
        {
            const auto [_StartX, _StartY] = Point2(
                Require(Segment_, "start", strPath_), strPath_ + ".start");
            const auto [_MiddleX, _MiddleY] = Point2(
                Require(Segment_, "middle", strPath_), strPath_ + ".middle");
            const auto [_EndX, _EndY] = Point2(
                Require(Segment_, "end", strPath_), strPath_ + ".end");
            return Arc(
                LocalPoint(Placement_, _StartX, _StartY),
                LocalPoint(Placement_, _MiddleX, _MiddleY),
                LocalPoint(Placement_, _EndX, _EndY), strPath_);
        }
        if (_Kind == "ellipseArc")
        {
            const auto [_CenterX, _CenterY] = Point2(
                Require(Segment_, "center", strPath_), strPath_ + ".center");
            const auto _MajorRadius = Number(
                Require(Segment_, "majorRadius", strPath_), strPath_ + ".majorRadius");
            const auto _MinorRadius = Number(
                Require(Segment_, "minorRadius", strPath_), strPath_ + ".minorRadius");
            const auto _Rotation = OptionalNumber(Segment_, "rotation", 0.0, strPath_);
            const auto _Start = Number(
                Require(Segment_, "startAngle", strPath_), strPath_ + ".startAngle");
            const auto _End = Number(
                Require(Segment_, "endAngle", strPath_), strPath_ + ".endAngle");
            if (_MajorRadius <= kTolerance || _MinorRadius <= kTolerance
                || _MinorRadius > _MajorRadius || _End - _Start <= kTolerance
                || _End - _Start > 2.0 * std::acos(-1.0) + kTolerance)
            {
                throw std::invalid_argument(strPath_ + " ellipse arc dimensions or angles are invalid");
            }
            const auto _MajorVector = gp_Vec(Placement_.XAxis) * std::cos(_Rotation)
                + gp_Vec(Placement_.YAxis) * std::sin(_Rotation);
            const gp_Dir _Normal(gp_Vec(Placement_.XAxis).Crossed(gp_Vec(Placement_.YAxis)));
            Handle(Geom_Ellipse) _Curve = new Geom_Ellipse(gp_Elips(
                gp_Ax2(
                    LocalPoint(Placement_, _CenterX, _CenterY),
                    _Normal, gp_Dir(_MajorVector)),
                _MajorRadius, _MinorRadius));
            BRepBuilderAPI_MakeEdge _Builder(_Curve, _Start, _End);
            if (!_Builder.IsDone())
                throw std::runtime_error(strPath_ + " failed to build an ellipse arc edge");
            return _Builder.Edge();
        }
        if (_Kind == "bezier")
        {
            const auto _Points = Point2List(
                Require(Segment_, "controlPoints", strPath_), strPath_ + ".controlPoints", 2);
            if (_Points.size() > 26)
                throw std::invalid_argument(strPath_ + ".controlPoints exceeds the supported degree");
            NCollection_Array1<gp_Pnt> _Poles(1, static_cast<int>(_Points.size()));
            for (int _Index = 1; _Index <= _Poles.Length(); ++_Index)
            {
                const auto& [_X, _Y] = _Points[static_cast<std::size_t>(_Index - 1)];
                _Poles.SetValue(_Index, LocalPoint(Placement_, _X, _Y));
            }
            Handle(Geom_BezierCurve) _Curve = new Geom_BezierCurve(_Poles);
            BRepBuilderAPI_MakeEdge _Builder(_Curve);
            if (!_Builder.IsDone())
                throw std::runtime_error(strPath_ + " failed to build a Bezier edge");
            return _Builder.Edge();
        }
        if (_Kind == "bspline") return MakeSplineEdge(Placement_, Segment_, false, strPath_);
        if (_Kind == "nurbs") return MakeSplineEdge(Placement_, Segment_, true, strPath_);
        throw std::invalid_argument(strPath_ + ".kind is unsupported: " + _Kind);
    }

    TopoDS_Wire MakePathWire(
        const SPlacement& Placement_, const ObjectMap& Contour_, const std::string& strPath_)
    {
        const auto& _Segments = RequireArray(
            Require(Contour_, "segments", strPath_), strPath_ + ".segments");
        if (_Segments.empty() || _Segments.size() > 10000)
            throw std::invalid_argument(strPath_ + ".segments has an invalid count");
        BRepBuilderAPI_MakeWire _Wire;
        for (std::size_t _Index = 0; _Index < _Segments.size(); ++_Index)
        {
            const auto _SegmentPath = strPath_ + ".segments[" + std::to_string(_Index) + "]";
            _Wire.Add(MakePathEdge(
                Placement_, RequireObject(_Segments[_Index], _SegmentPath), _SegmentPath));
            if (!_Wire.IsDone())
                throw std::invalid_argument(_SegmentPath + " is disconnected from the path");
        }
        if (!BRep_Tool::IsClosed(_Wire.Wire()))
            throw std::invalid_argument(strPath_ + " path must be closed");
        return _Wire.Wire();
    }

    TopoDS_Wire MakePolygonWire(
        const SPlacement& Placement_, const std::vector<std::pair<double, double>>& Points_,
        const std::string& strPath_)
    {
        BRepBuilderAPI_MakePolygon _Polygon;
        for (const auto& [_X, _Y] : Points_) _Polygon.Add(LocalPoint(Placement_, _X, _Y));
        _Polygon.Close();
        if (!_Polygon.IsDone()) throw std::runtime_error(strPath_ + " failed to build a closed polygon");
        return _Polygon.Wire();
    }

    TopoDS_Wire MakeContourWire(
        const SPlacement& Placement_, const ObjectMap& Contour_, const std::string& strPath_)
    {
        const auto _Kind = RequireString(Contour_, "kind", strPath_);
        if (_Kind == "path") return MakePathWire(Placement_, Contour_, strPath_);
        if (_Kind == "circle")
        {
            const auto _Radius = Number(
                Require(Contour_, "radius", strPath_), strPath_ + ".radius");
            if (_Radius <= kTolerance)
                throw std::invalid_argument(strPath_ + ".radius must be positive");
            const gp_Dir _Normal(gp_Vec(Placement_.XAxis).Crossed(gp_Vec(Placement_.YAxis)));
            BRepBuilderAPI_MakeEdge _Edge(gp_Circ(
                gp_Ax2(Placement_.Origin, _Normal, Placement_.XAxis), _Radius));
            if (!_Edge.IsDone()) throw std::runtime_error(strPath_ + " failed to build a circle");
            BRepBuilderAPI_MakeWire _Wire(_Edge.Edge());
            if (!_Wire.IsDone())
                throw std::runtime_error(strPath_ + " failed to build a circular wire");
            return _Wire.Wire();
        }
        if (_Kind == "ellipse")
        {
            const auto _Width = Number(
                Require(Contour_, "width", strPath_), strPath_ + ".width");
            const auto _Height = Number(
                Require(Contour_, "height", strPath_), strPath_ + ".height");
            if (_Width <= kTolerance || _Height <= kTolerance)
                throw std::invalid_argument(strPath_ + " ellipse dimensions must be positive");
            const gp_Dir _Normal(gp_Vec(Placement_.XAxis).Crossed(gp_Vec(Placement_.YAxis)));
            const auto _WidthIsMajor = _Width >= _Height;
            const auto _MajorRadius = std::max(_Width, _Height) / 2.0;
            const auto _MinorRadius = std::min(_Width, _Height) / 2.0;
            const auto& _MajorAxis = _WidthIsMajor ? Placement_.XAxis : Placement_.YAxis;
            BRepBuilderAPI_MakeEdge _Edge(gp_Elips(
                gp_Ax2(Placement_.Origin, _Normal, _MajorAxis), _MajorRadius, _MinorRadius));
            if (!_Edge.IsDone()) throw std::runtime_error(strPath_ + " failed to build an ellipse");
            BRepBuilderAPI_MakeWire _Wire(_Edge.Edge());
            if (!_Wire.IsDone())
                throw std::runtime_error(strPath_ + " failed to build an elliptical wire");
            return _Wire.Wire();
        }
        if (_Kind == "capsule")
        {
            const auto _Width = Number(
                Require(Contour_, "width", strPath_), strPath_ + ".width");
            const auto _Height = Number(
                Require(Contour_, "height", strPath_), strPath_ + ".height");
            if (_Width <= kTolerance || _Height <= kTolerance)
                throw std::invalid_argument(strPath_ + " capsule dimensions must be positive");
            const auto _Point = [&](double X_, double Y_) { return LocalPoint(Placement_, X_, Y_); };
            if (std::abs(_Width - _Height) <= kTolerance)
            {
                const gp_Dir _Normal(gp_Vec(Placement_.XAxis).Crossed(gp_Vec(Placement_.YAxis)));
                BRepBuilderAPI_MakeEdge _Edge(gp_Circ(
                    gp_Ax2(Placement_.Origin, _Normal, Placement_.XAxis), _Width / 2.0));
                if (!_Edge.IsDone())
                    throw std::runtime_error(strPath_ + " failed to build a circular capsule");
                BRepBuilderAPI_MakeWire _Wire(_Edge.Edge());
                if (!_Wire.IsDone())
                    throw std::runtime_error(strPath_ + " failed to build a circular capsule");
                return _Wire.Wire();
            }
            BRepBuilderAPI_MakeWire _Wire;
            if (_Width > _Height)
            {
                const auto _Radius = _Height / 2.0;
                const auto _Center = (_Width - _Height) / 2.0;
                const auto _BottomLeft = _Point(-_Center, -_Radius);
                const auto _BottomRight = _Point(_Center, -_Radius);
                const auto _TopRight = _Point(_Center, _Radius);
                const auto _TopLeft = _Point(-_Center, _Radius);
                _Wire.Add(Line(_BottomLeft, _BottomRight, strPath_));
                _Wire.Add(Arc(_BottomRight, _Point(_Center + _Radius, 0.0), _TopRight, strPath_));
                _Wire.Add(Line(_TopRight, _TopLeft, strPath_));
                _Wire.Add(Arc(_TopLeft, _Point(-_Center - _Radius, 0.0), _BottomLeft, strPath_));
            }
            else
            {
                const auto _Radius = _Width / 2.0;
                const auto _Center = (_Height - _Width) / 2.0;
                const auto _BottomRight = _Point(_Radius, -_Center);
                const auto _TopRight = _Point(_Radius, _Center);
                const auto _TopLeft = _Point(-_Radius, _Center);
                const auto _BottomLeft = _Point(-_Radius, -_Center);
                _Wire.Add(Line(_BottomRight, _TopRight, strPath_));
                _Wire.Add(Arc(_TopRight, _Point(0.0, _Center + _Radius), _TopLeft, strPath_));
                _Wire.Add(Line(_TopLeft, _BottomLeft, strPath_));
                _Wire.Add(Arc(_BottomLeft, _Point(0.0, -_Center - _Radius), _BottomRight, strPath_));
            }
            if (!_Wire.IsDone()) throw std::runtime_error(strPath_ + " failed to build a capsule wire");
            return _Wire.Wire();
        }
        if (_Kind == "polygon")
        {
            const auto& _PointValues = RequireArray(
                Require(Contour_, "points", strPath_), strPath_ + ".points");
            if (_PointValues.size() < 3)
                throw std::invalid_argument(strPath_ + ".points requires at least three points");
            std::vector<std::pair<double, double>> _Points;
            _Points.reserve(_PointValues.size());
            for (std::size_t _Index = 0; _Index < _PointValues.size(); ++_Index)
                _Points.push_back(Point2(_PointValues[_Index],
                    strPath_ + ".points[" + std::to_string(_Index) + "]"));
            return MakePolygonWire(Placement_, _Points, strPath_);
        }
        if (_Kind != "roundedRectangle")
            throw std::invalid_argument(strPath_ + ".kind is unsupported: " + _Kind);

        const auto _Width = Number(Require(Contour_, "width", strPath_), strPath_ + ".width");
        const auto _Height = Number(Require(Contour_, "height", strPath_), strPath_ + ".height");
        const auto _Radius = OptionalNumber(Contour_, "radius", 0.0, strPath_);
        if (_Width <= kTolerance || _Height <= kTolerance || _Radius < 0.0
            || _Radius >= std::min(_Width, _Height) / 2.0)
        {
            throw std::invalid_argument(strPath_ + " rounded rectangle dimensions are invalid");
        }
        const auto _HalfWidth = _Width / 2.0;
        const auto _HalfHeight = _Height / 2.0;
        if (_Radius <= kTolerance)
        {
            return MakePolygonWire(Placement_, {
                { -_HalfWidth, -_HalfHeight }, { _HalfWidth, -_HalfHeight },
                { _HalfWidth, _HalfHeight }, { -_HalfWidth, _HalfHeight }
            }, strPath_);
        }

        const auto _Diagonal = _Radius / std::sqrt(2.0);
        const auto _Point = [&](double X_, double Y_) { return LocalPoint(Placement_, X_, Y_); };
        const auto _BottomLeft = _Point(-_HalfWidth + _Radius, -_HalfHeight);
        const auto _BottomRight = _Point(_HalfWidth - _Radius, -_HalfHeight);
        const auto _RightBottom = _Point(_HalfWidth, -_HalfHeight + _Radius);
        const auto _RightTop = _Point(_HalfWidth, _HalfHeight - _Radius);
        const auto _TopRight = _Point(_HalfWidth - _Radius, _HalfHeight);
        const auto _TopLeft = _Point(-_HalfWidth + _Radius, _HalfHeight);
        const auto _LeftTop = _Point(-_HalfWidth, _HalfHeight - _Radius);
        const auto _LeftBottom = _Point(-_HalfWidth, -_HalfHeight + _Radius);
        BRepBuilderAPI_MakeWire _Wire;
        _Wire.Add(Line(_BottomLeft, _BottomRight, strPath_));
        _Wire.Add(Arc(_BottomRight, _Point(_HalfWidth - _Radius + _Diagonal, -_HalfHeight + _Radius - _Diagonal), _RightBottom, strPath_));
        _Wire.Add(Line(_RightBottom, _RightTop, strPath_));
        _Wire.Add(Arc(_RightTop, _Point(_HalfWidth - _Radius + _Diagonal, _HalfHeight - _Radius + _Diagonal), _TopRight, strPath_));
        _Wire.Add(Line(_TopRight, _TopLeft, strPath_));
        _Wire.Add(Arc(_TopLeft, _Point(-_HalfWidth + _Radius - _Diagonal, _HalfHeight - _Radius + _Diagonal), _LeftTop, strPath_));
        _Wire.Add(Line(_LeftTop, _LeftBottom, strPath_));
        _Wire.Add(Arc(_LeftBottom, _Point(-_HalfWidth + _Radius - _Diagonal, -_HalfHeight + _Radius - _Diagonal), _BottomLeft, strPath_));
        if (!_Wire.IsDone())
            throw std::runtime_error(strPath_ + " failed to build a rounded rectangle wire");
        return _Wire.Wire();
    }

    TopoDS_Face MakeProfile2D(const SGeometryNode& Node_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key;
        const auto _Placement = Placement(Node_.Arguments, _Path);
        const auto& _ContourValues = RequireArray(
            Require(Node_.Arguments, "contours", _Path), _Path + ".contours");
        if (_ContourValues.empty() || _ContourValues.size() > 10000)
            throw std::invalid_argument(_Path + ".contours has an invalid count");

        std::vector<TopoDS_Wire> _Wires;
        _Wires.reserve(_ContourValues.size());
        for (std::size_t _Index = 0; _Index < _ContourValues.size(); ++_Index)
        {
            const auto _ContourPath = _Path + ".contours[" + std::to_string(_Index) + "]";
            _Wires.push_back(MakeContourWire(
                _Placement, RequireObject(_ContourValues[_Index], _ContourPath), _ContourPath));
        }

        BRepBuilderAPI_MakeFace _Face(_Wires.front(), true);
        if (!_Face.IsDone())
            throw std::runtime_error(_Path + " failed to build the outer profile face");
        for (std::size_t _Index = 1; _Index < _Wires.size(); ++_Index)
        {
            _Face.Add(_Wires[_Index]);
            if (!_Face.IsDone())
                throw std::runtime_error(
                    _Path + ".contours[" + std::to_string(_Index) + "] failed to add a hole");
        }
        ShapeFix_Face _Fixer(_Face.Face());
        _Fixer.FixOrientation();
        const auto _Result = _Fixer.Face();
        if (_Result.IsNull()) throw std::runtime_error(_Path + " failed to build a profile face");
        return _Result;
    }

    TopoDS_Shape MakeExtrude(const SGeometryNode& Node_, const TopoDS_Shape& Profile_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key;
        auto _Vector = Vector3(Require(Node_.Arguments, "vector", _Path), _Path + ".vector");
        const auto _Length = _Vector.Magnitude();
        if (_Length <= kTolerance) throw std::invalid_argument(_Path + ".vector cannot be zero");
        const auto _ExtendStart = OptionalNumber(Node_.Arguments, "extendStart", 0.0, _Path);
        const auto _ExtendEnd = OptionalNumber(Node_.Arguments, "extendEnd", 0.0, _Path);
        if (_ExtendStart < 0.0 || _ExtendEnd < 0.0)
            throw std::invalid_argument(_Path + " extrusion extensions cannot be negative");
        const gp_Vec _Unit = _Vector / _Length;
        auto _StartShape = Profile_;
        if (_ExtendStart > 0.0)
        {
            gp_Trsf _Translation;
            _Translation.SetTranslation(-_Unit * _ExtendStart);
            _StartShape = BRepBuilderAPI_Transform(Profile_, _Translation, true).Shape();
        }
        _Vector = _Unit * (_Length + _ExtendStart + _ExtendEnd);
        BRepPrimAPI_MakePrism _Prism(_StartShape, _Vector, false, true);
        if (!_Prism.IsDone() || _Prism.Shape().IsNull())
            throw std::runtime_error(_Path + " failed to extrude its input");
        return _Prism.Shape();
    }

    TopoDS_Shape MakeTransform(const SGeometryNode& Node_, const TopoDS_Shape& Input_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key + ".placement";
        const auto& _Placement = RequireObject(
            Require(Node_.Arguments, "placement", "geometry." + Node_.Key), _Path);
        const auto _Origin = Point3(Require(_Placement, "origin", _Path), _Path + ".origin");
        const auto _X = Vector3(Require(_Placement, "xAxis", _Path), _Path + ".xAxis");
        const auto _Y = Vector3(Require(_Placement, "yAxis", _Path), _Path + ".yAxis");
        const auto _Z = Vector3(Require(_Placement, "zAxis", _Path), _Path + ".zAxis");
        constexpr double _AxisTolerance = 1.0e-7;
        if (std::abs(_X.SquareMagnitude() - 1.0) > _AxisTolerance
            || std::abs(_Y.SquareMagnitude() - 1.0) > _AxisTolerance
            || std::abs(_Z.SquareMagnitude() - 1.0) > _AxisTolerance)
        {
            throw std::invalid_argument(_Path + " axes must be unit vectors; scaling is not supported");
        }
        if (std::abs(_X.Dot(_Y)) > _AxisTolerance
            || std::abs(_X.Dot(_Z)) > _AxisTolerance
            || std::abs(_Y.Dot(_Z)) > _AxisTolerance)
        {
            throw std::invalid_argument(_Path + " axes must be orthogonal");
        }
        if (std::abs(_X.Crossed(_Y).Dot(_Z) - 1.0) > _AxisTolerance)
            throw std::invalid_argument(_Path + " axes must be right-handed; mirroring is not supported");

        // Build a pure rigid transform after validating the supplied frame. Ax3
        // removes round-off only; no scale/shear/mirror is silently accepted.
        gp_Trsf _Transform;
        _Transform.SetDisplacement(gp_Ax3(), gp_Ax3(_Origin, gp_Dir(_Z), gp_Dir(_X)));
        // Moved returns a separate location/orientation wrapper over the same
        // TShape. Do not deep-copy the shared profile extrusion per instance.
        return Input_.Moved(TopLoc_Location(_Transform), true);
    }

    TopoDS_Shape BooleanShape(
        const SGeometryNode& Node_,
        const std::function<const TopoDS_Shape&(const std::string&)>& Resolve_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key;
        const auto _Operation = RequireString(Node_.Arguments, "operation", _Path);
        std::string _TargetKey;
        if (const auto _Target = Find(Node_.Arguments, "target"))
        {
            if (!_Target->Is<std::string>()) throw std::invalid_argument(_Path + ".target must be a string");
            _TargetKey = _Target->To<std::string>();
        }
        else if (!Node_.Inputs.empty()) _TargetKey = Node_.Inputs.front();
        if (_TargetKey.empty()) throw std::invalid_argument(_Path + " requires a target");

        std::vector<std::string> _ToolKeys;
        if (const auto _Tools = Find(Node_.Arguments, "tools"))
        {
            std::size_t _Index = 0;
            for (const auto& _Value : RequireArray(*_Tools, _Path + ".tools"))
            {
                if (!_Value.Is<std::string>() || _Value.To<std::string>().empty())
                    throw std::invalid_argument(_Path + ".tools[" + std::to_string(_Index) + "] must be a string");
                _ToolKeys.push_back(_Value.To<std::string>());
                ++_Index;
            }
        }
        else if (Node_.Inputs.size() > 1)
        {
            _ToolKeys.assign(Node_.Inputs.begin() + 1, Node_.Inputs.end());
        }
        if (_ToolKeys.empty()) throw std::invalid_argument(_Path + " requires at least one tool");

        auto _Result = Resolve_(_TargetKey);
        for (const auto& _ToolKey : _ToolKeys)
        {
            const auto& _Tool = Resolve_(_ToolKey);
            const auto _BuildWithoutChangingInputs = [&](auto& Builder_) {
                // Constructors taking two shapes execute immediately. Set this
                // mode before Build so shared, located instances are never cut
                // or tolerance-modified in place through their common TShape.
                Builder_.SetNonDestructive(true);
                NCollection_List<TopoDS_Shape> _Arguments;
                _Arguments.Append(_Result);
                NCollection_List<TopoDS_Shape> _Tools;
                _Tools.Append(_Tool);
                Builder_.SetArguments(_Arguments);
                Builder_.SetTools(_Tools);
                Builder_.Build();
            };
            if (_Operation == "subtract")
            {
                BRepAlgoAPI_Cut _Builder;
                _BuildWithoutChangingInputs(_Builder);
                if (!_Builder.IsDone()) throw std::runtime_error(_Path + " subtraction failed");
                _Result = _Builder.Shape();
            }
            else if (_Operation == "union")
            {
                BRepAlgoAPI_Fuse _Builder;
                _BuildWithoutChangingInputs(_Builder);
                if (!_Builder.IsDone()) throw std::runtime_error(_Path + " union failed");
                _Result = _Builder.Shape();
            }
            else if (_Operation == "intersect")
            {
                BRepAlgoAPI_Common _Builder;
                _BuildWithoutChangingInputs(_Builder);
                if (!_Builder.IsDone()) throw std::runtime_error(_Path + " intersection failed");
                _Result = _Builder.Shape();
            }
            else throw std::invalid_argument(_Path + ".operation is unsupported: " + _Operation);
            if (_Result.IsNull()) throw std::runtime_error(_Path + " produced an empty shape");
        }
        return _Result;
    }
}

const TopoDS_Shape& iCAX::OpenCascade::SNeutralModelEvaluation::At(
    const std::string& strGeometryKey_) const
{
    const auto _Iterator = Geometry.find(strGeometryKey_);
    if (_Iterator == Geometry.end())
        throw std::out_of_range("neutral model geometry was not evaluated: " + strGeometryKey_);
    return _Iterator->second;
}

iCAX::OpenCascade::SNeutralModelEvaluation iCAX::OpenCascade::EvaluateNeutralModel(
    const SNeutralModel& Model_)
{
    std::vector<std::string> _GeometryKeys;
    _GeometryKeys.reserve(Model_.Geometry.size());
    for (const auto& _Node : Model_.Geometry) _GeometryKeys.push_back(_Node.Key);
    return EvaluateNeutralModel(Model_, _GeometryKeys);
}

iCAX::OpenCascade::SNeutralModelEvaluation iCAX::OpenCascade::EvaluateNeutralModel(
    const SNeutralModel& Model_,
    const std::vector<std::string>& GeometryKeys_)
{
    std::unordered_map<std::string, const SGeometryNode*> _Nodes;
    for (const auto& _Node : Model_.Geometry)
        if (!_Nodes.emplace(_Node.Key, &_Node).second)
            throw std::invalid_argument("duplicate neutral model geometry key: " + _Node.Key);

    SNeutralModelEvaluation _Result;
    std::unordered_set<std::string> _Visiting;
    std::function<const TopoDS_Shape&(const std::string&)> _Evaluate;
    _Evaluate = [&](const std::string& strKey_) -> const TopoDS_Shape& {
        if (const auto _Existing = _Result.Geometry.find(strKey_); _Existing != _Result.Geometry.end())
            return _Existing->second;
        const auto _NodeIterator = _Nodes.find(strKey_);
        if (_NodeIterator == _Nodes.end())
            throw std::invalid_argument("neutral model references missing geometry: " + strKey_);
        if (!_Visiting.emplace(strKey_).second)
            throw std::invalid_argument("neutral model geometry dependency cycle: " + strKey_);
        const auto& _Node = *_NodeIterator->second;
        TopoDS_Shape _Shape;
        switch (_Node.Operator)
        {
        case EGeometryOperator::Profile2D:
            _Shape = MakeProfile2D(_Node);
            break;
        case EGeometryOperator::Extrude:
            if (_Node.Inputs.size() != 1)
                throw std::invalid_argument("geometry." + _Node.Key + " extrude requires exactly one input");
            _Shape = MakeExtrude(_Node, _Evaluate(_Node.Inputs.front()));
            break;
        case EGeometryOperator::Boolean:
            _Shape = BooleanShape(_Node, _Evaluate);
            break;
        case EGeometryOperator::Transform:
            if (_Node.Inputs.size() != 1)
                throw std::invalid_argument("geometry." + _Node.Key + " transform requires exactly one input");
            _Shape = MakeTransform(_Node, _Evaluate(_Node.Inputs.front()));
            break;
        case EGeometryOperator::Compound:
        {
            if (_Node.Inputs.empty())
                throw std::invalid_argument("geometry." + _Node.Key + " compound requires inputs");
            TopoDS_Compound _Compound;
            BRep_Builder _Builder;
            _Builder.MakeCompound(_Compound);
            for (const auto& _Input : _Node.Inputs) _Builder.Add(_Compound, _Evaluate(_Input));
            _Shape = _Compound;
            break;
        }
        default:
            throw std::invalid_argument("geometry." + _Node.Key + " uses an operator not implemented by the OpenCascade adapter");
        }
        if (_Shape.IsNull()) throw std::runtime_error("geometry." + _Node.Key + " evaluated to a null shape");
        _Visiting.erase(strKey_);
        return _Result.Geometry.emplace(_Node.Key, std::move(_Shape)).first->second;
    };
    for (const auto& _GeometryKey : GeometryKeys_) (void)_Evaluate(_GeometryKey);
    return _Result;
}
