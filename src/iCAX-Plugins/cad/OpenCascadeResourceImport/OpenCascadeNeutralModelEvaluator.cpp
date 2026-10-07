#include "pch.h"

#include "OpenCascadeNeutralModelEvaluator.h"
#include "OpenCascadeTaskExecution.h"
#include "OpenCascadeCancellation.h"
#include <TemplateRuntime/StandardJsonCodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Ellipse.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_Face.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
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
#include <TopoDS_Iterator.hxx>
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

    using iCAX::OpenCascade::detail::GeometryTaskScheduler;
    using iCAX::OpenCascade::detail::SGeometryTaskBatch;

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
            Handle(Geom_BezierCurve) _Curve;
            if (const auto _Values = Find(Segment_, "weights"))
            {
                const auto _Weights = NumberList(*_Values, strPath_ + ".weights", 2);
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
                _Curve = new Geom_BezierCurve(_Poles, _WeightArray);
            }
            else _Curve = new Geom_BezierCurve(_Poles);
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
            const auto& _Segment = RequireObject(_Segments[_Index], _SegmentPath);
            auto _Edge = MakePathEdge(Placement_, _Segment, _SegmentPath);
            // Keep exact spline poles/knots and its trimmed parameter interval;
            // traversal direction belongs to the edge, not a resampled curve.
            if (OptionalBoolean(_Segment, "reversed", false, _SegmentPath)) _Edge.Reverse();
            _Wire.Add(_Edge);
            if (!_Wire.IsDone())
                throw std::invalid_argument(_SegmentPath + " is disconnected from the path");
        }
        if (!BRep_Tool::IsClosed(_Wire.Wire()))
            throw std::invalid_argument(strPath_ + " path must be closed");
        return _Wire.Wire();
    }

    TopoDS_Wire MakeContourWire(
        const SPlacement& Placement_, const ObjectMap& Contour_, const std::string& strPath_)
    {
        const auto _Kind = RequireString(Contour_, "kind", strPath_);
        if (_Kind != "path")
            throw std::invalid_argument(strPath_ + ".kind must be path; profile2d accepts only generic curves");
        return MakePathWire(Placement_, Contour_, strPath_);
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

        // A profile is one connected material region with any number of voids.
        // Roles come from exact containment, never array order or profile IDs.
        std::vector<TopoDS_Face> _LoopFaces;
        for (std::size_t _Index = 0; _Index < _Wires.size(); ++_Index)
        {
            BRepBuilderAPI_MakeFace _Loop(_Wires[_Index], true);
            if (!_Loop.IsDone() || !BRepCheck_Analyzer(_Loop.Face()).IsValid())
                throw std::invalid_argument(_Path + ".contours[" + std::to_string(_Index)
                    + "] is not a valid simple closed boundary");
            ShapeFix_Face _Orientation(_Loop.Face());
            _Orientation.FixOrientation();
            _LoopFaces.push_back(_Orientation.Face());
        }
        std::vector<std::size_t> _Depth(_Wires.size(), 0);
        for (std::size_t _I = 0; _I < _Wires.size(); ++_I)
        {
            for (std::size_t _J = _I + 1; _J < _Wires.size(); ++_J)
            {
                BRepExtrema_DistShapeShape _Distance(_Wires[_I], _Wires[_J]);
                if (!_Distance.IsDone() || _Distance.Value() <= kTolerance)
                    throw std::invalid_argument(_Path + ".contours[" + std::to_string(_I)
                        + "] and contours[" + std::to_string(_J) + "] intersect or touch");
            }
            TopExp_Explorer _Vertices(_Wires[_I], TopAbs_VERTEX);
            if (!_Vertices.More()) throw std::invalid_argument(_Path + " boundary has no vertex");
            const auto _Point = BRep_Tool::Pnt(TopoDS::Vertex(_Vertices.Current()));
            for (std::size_t _J = 0; _J < _Wires.size(); ++_J)
            {
                if (_I == _J) continue;
                BRepClass_FaceClassifier _Classifier(_LoopFaces[_J], _Point, kTolerance);
                if (_Classifier.State() == TopAbs_IN) ++_Depth[_I];
                else if (_Classifier.State() != TopAbs_OUT)
                    throw std::invalid_argument(_Path + " ambiguous contour containment");
            }
        }
        if (std::count(_Depth.begin(), _Depth.end(), 0) != 1
            || std::any_of(_Depth.begin(), _Depth.end(), [](auto _Value) { return _Value > 1; }))
            throw std::invalid_argument(_Path + " requires one connected material region; separate regions or nested islands must be separate profiles");
        const auto _Outer = static_cast<std::size_t>(std::find(_Depth.begin(), _Depth.end(), 0) - _Depth.begin());
        BRepBuilderAPI_MakeFace _Face(_Wires[_Outer], true);
        if (!_Face.IsDone())
            throw std::runtime_error(_Path + " failed to build the outer profile face");
        for (std::size_t _Index = 0; _Index < _Wires.size(); ++_Index)
        {
            if (_Index == _Outer) continue;
            _Face.Add(_Wires[_Index]);
            if (!_Face.IsDone())
                throw std::runtime_error(
                    _Path + ".contours[" + std::to_string(_Index) + "] failed to add a hole");
        }
        ShapeFix_Face _Fixer(_Face.Face());
        _Fixer.FixOrientation();
        const auto _Result = _Fixer.Face();
        if (_Result.IsNull() || !BRepCheck_Analyzer(_Result).IsValid())
            throw std::runtime_error(_Path + " failed to build a valid profile material region");
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

    TopoDS_Shape MakeResource(const SGeometryNode& Node_)
    {
        const auto _Path = "geometry." + Node_.Key + ".arguments.brep";
        if (!Node_.Inputs.empty())
            throw std::invalid_argument("geometry." + Node_.Key + " resource must not have inputs");
        const auto _Value = Find(Node_.Arguments, "brep");
        if (!_Value || !_Value->Is<std::string>())
            throw std::invalid_argument(_Path + " requires resolved BRepTools text; references, paths and URLs are not read by the geometry evaluator");
        const auto& _Text = std::get<std::string>(_Value->m_Value);
        if (_Text.empty() || _Text.size() > iCAX::TemplateRuntime::kMaximumResourceBRepBytes)
            throw std::invalid_argument(_Path + " must contain 1 to 33554432 bytes (32 MiB)");
        if (std::any_of(_Text.begin(), _Text.end(), [](unsigned char Character_) {
            return Character_ > 126 || (Character_ < 32 && Character_ != '\n'
                && Character_ != '\r' && Character_ != '\t' && Character_ != '\f' && Character_ != '\v');
        }))
            throw std::invalid_argument(_Path + " must be ASCII BRepTools text, not binary data");
        std::string_view _Header(_Text);
        const auto _Start = _Header.find_first_not_of(" \t\r\n\f\v");
        if (_Start == std::string_view::npos)
            throw std::invalid_argument(_Path + " must not be blank");
        _Header.remove_prefix(_Start);
        if (!_Header.starts_with("CASCADE Topology V1,")
            && !_Header.starts_with("CASCADE Topology V2,")
            && !_Header.starts_with("CASCADE Topology V3,"))
            throw std::invalid_argument(_Path + " must use the ASCII BRepTools CASCADE Topology V1/V2/V3 format");

        try
        {
            std::istringstream _Stream(_Text);
            BRep_Builder _Builder;
            TopoDS_Shape _Shape;
            BRepTools::Read(_Shape, _Stream, _Builder);
            if (_Stream.fail() || _Shape.IsNull())
                throw std::invalid_argument(_Path + " could not be read as a non-empty BRep shape");
            _Stream >> std::ws;
            if (_Stream.peek() != std::char_traits<char>::eof())
                throw std::invalid_argument(_Path + " contains trailing data after the BRep shape");
            iCAX::OpenCascade::ValidateSolidResourceShape(_Shape, _Path);
            return _Shape;
        }
        catch (const Standard_Failure& Failure_)
        {
            const char* _Message = Failure_.what();
            throw std::invalid_argument(_Path + " is not a usable solid BRep: " + (_Message ? _Message : "OpenCascade rejected the geometry"));
        }
    }

    gp_Trsf RigidNodeTransform(const SGeometryNode& Node_)
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
        return _Transform;
    }

    TopoDS_Shape MakeTransform(const SGeometryNode& Node_, const TopoDS_Shape& Input_)
    {
        // Moved returns a separate location/orientation wrapper over the same
        // TShape. Do not deep-copy the shared profile extrusion per instance.
        return Input_.Moved(TopLoc_Location(RigidNodeTransform(Node_)), true);
    }

    std::vector<std::string> BooleanInputs(const SGeometryNode& Node_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key;
        const auto _Operation = RequireString(Node_.Arguments, "operation", _Path);
        if (_Operation != "subtract" && _Operation != "union" && _Operation != "intersect")
            throw std::invalid_argument(_Path + ".operation is unsupported: " + _Operation);
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
        _ToolKeys.insert(_ToolKeys.begin(), std::move(_TargetKey));
        return _ToolKeys;
    }

    TopoDS_Shape BooleanShape(
        const SGeometryNode& Node_,
        const std::function<const TopoDS_Shape&(const std::string&)>& Resolve_,
        bool UseBoundingBoxFilter_,
        bool SuppressUnusedHistory_,
        bool UseOrientedBoundingBoxes_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key;
        const auto _Operation = RequireString(Node_.Arguments, "operation", _Path);
        const auto _Inputs = BooleanInputs(Node_);

        auto _Result = Resolve_(_Inputs.front());
        Bnd_Box _TargetBox;
        // Subtraction cannot enlarge its target. Reuse the initial conservative
        // bound for the whole tool chain instead of bounding each increasingly
        // complex cut result again (which can cost more than the quick rejection).
        if (UseBoundingBoxFilter_ && _Operation == "subtract")
            BRepBndLib::Add(_Result, _TargetBox, false);
        // One OCCT cut can classify all tools against the target together.
        // Sequential cuts repeatedly rebuild the increasingly complex result.
        if (_Operation == "subtract" && _Inputs.size() > 2)
        {
            NCollection_List<TopoDS_Shape> _Tools;
            for (std::size_t _Index = 1; _Index < _Inputs.size(); ++_Index)
            {
                const auto& _Tool = Resolve_(_Inputs[_Index]);
                if (UseBoundingBoxFilter_)
                {
                    Bnd_Box _ToolBox;
                    BRepBndLib::Add(_Tool, _ToolBox, false);
                    if (!_TargetBox.IsVoid() && !_ToolBox.IsVoid()
                        && _TargetBox.IsOut(_ToolBox)) continue;
                }
                _Tools.Append(_Tool);
            }
            if (!_Tools.IsEmpty())
            {
                BRepAlgoAPI_Cut _Builder;
                _Builder.SetNonDestructive(true);
                _Builder.SetRunParallel(false);
                _Builder.SetFuzzyValue(1.e-6);
                if (SuppressUnusedHistory_) _Builder.SetToFillHistory(false);
                if (UseOrientedBoundingBoxes_) _Builder.SetUseOBB(true);
                NCollection_List<TopoDS_Shape> _Arguments;
                _Arguments.Append(_Result);
                _Builder.SetArguments(_Arguments);
                _Builder.SetTools(_Tools);
                iCAX::OpenCascade::COpenCascadeCancellationScope::Build(_Builder);
                if (!_Builder.IsDone()) throw std::runtime_error(_Path + " subtraction failed");
                _Result = _Builder.Shape();
                if (_Result.IsNull()) throw std::runtime_error(_Path + " produced an empty shape");
            }
        }
        else
        for (std::size_t _Index = 1; _Index < _Inputs.size(); ++_Index)
        {
            const auto& _Tool = Resolve_(_Inputs[_Index]);
            if (UseBoundingBoxFilter_ && _Operation == "subtract")
            {
                // Conservative, tolerance-expanded bounds, independent of any
                // display triangulation. Touching/overlapping boxes still go to
                // OCCT; never use this shortcut for fuse or common.
                Bnd_Box _ToolBox;
                BRepBndLib::Add(_Tool, _ToolBox, false);
                if (!_TargetBox.IsVoid() && !_ToolBox.IsVoid()
                    && _TargetBox.IsOut(_ToolBox)) continue;
            }
            const auto _BuildWithoutChangingInputs = [&](auto& Builder_) {
                // Constructors taking two shapes execute immediately. Set this
                // mode before Build so shared, located instances are never cut
                // or tolerance-modified in place through their common TShape.
                Builder_.SetNonDestructive(true);
                Builder_.SetRunParallel(false);
                // Millimetre geometry: use the same 1 nm coincidence tolerance
                // for cut/fuse/common. Default per-shape tolerances can leave
                // sliver solids at successive miter/elliptic intersections.
                Builder_.SetFuzzyValue(1.e-6);
                if (SuppressUnusedHistory_) Builder_.SetToFillHistory(false);
                if (UseOrientedBoundingBoxes_) Builder_.SetUseOBB(true);
                NCollection_List<TopoDS_Shape> _Arguments;
                _Arguments.Append(_Result);
                NCollection_List<TopoDS_Shape> _Tools;
                _Tools.Append(_Tool);
                Builder_.SetArguments(_Arguments);
                Builder_.SetTools(_Tools);
                iCAX::OpenCascade::COpenCascadeCancellationScope::Build(Builder_);
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
        if (const auto _Seed = Find(Node_.Arguments, "keepConnectedTo"))
        {
            // Explicit material witness, not a largest-fragment heuristic. The
            // node author identifies the retained manufactured piece; detached
            // machining scrap is discarded only when the witness is unique.
            const auto _Point = Point3(*_Seed, _Path + ".keepConnectedTo");
            TopoDS_Shape _Retained;
            for (TopExp_Explorer _It(_Result, TopAbs_SOLID); _It.More(); _It.Next())
            {
                BRepClass3d_SolidClassifier _Classifier(_It.Current(), _Point, kTolerance);
                if (_Classifier.State() != TopAbs_IN && _Classifier.State() != TopAbs_ON) continue;
                if (!_Retained.IsNull()) throw std::runtime_error(_Path + " keepConnectedTo is ambiguous");
                _Retained = _It.Current();
            }
            if (_Retained.IsNull()) throw std::runtime_error(_Path + " keepConnectedTo is not on retained material");
            _Result = _Retained;
        }
        return _Result;
    }

    ObjectMap RigidPlacementArguments(const gp_Trsf& Transform_)
    {
        const auto _Value = [&](int Row_, int Column_) {
            const auto _Coordinate = Transform_.Value(Row_, Column_);
            return _Coordinate == 0.0 ? 0.0 : _Coordinate;
        };
        return ObjectMap{{"placement",ObjectMap{
            {"origin",VariantArray{_Value(1,4),_Value(2,4),_Value(3,4)}},
            {"xAxis",VariantArray{_Value(1,1),_Value(2,1),_Value(3,1)}},
            {"yAxis",VariantArray{_Value(1,2),_Value(2,2),_Value(3,2)}},
        {"zAxis",VariantArray{_Value(1,3),_Value(2,3),_Value(3,3)}}}}};
    }

    bool IsExactIdentityTransform(const gp_Trsf& Transform_)
    {
        // gp_Trsf can retain its translation/compound form after multiplying
        // exact inverse frames. Require every matrix coefficient to equal the
        // identity; never round a nearly-identity geometric placement.
        for (int _Row = 1; _Row <= 3; ++_Row)
            for (int _Column = 1; _Column <= 4; ++_Column)
                if (Transform_.Value(_Row, _Column) != (_Row == _Column ? 1.0 : 0.0)) return false;
        return true;
    }

    bool IsPlainRigidTransform(const SGeometryNode& Node_)
    {
        if (Node_.Operator != EGeometryOperator::Transform || Node_.Arguments.size() != 1
            || !Node_.Arguments.contains("placement")) return false;
        const auto& _Placement = RequireObject(Node_.Arguments.at("placement"), "geometry."+Node_.Key+".placement");
        return std::all_of(_Placement.begin(), _Placement.end(), [](const auto& Field_) {
            return Field_.first == "origin" || Field_.first == "xAxis" || Field_.first == "yAxis" || Field_.first == "zAxis";
        });
    }

    // Factoring proper rigid transforms is an exact geometric identity, not a
    // size-based cache. The complete local contour/curve data, extrusion vector,
    // operation arguments and relative placements remain in each prototype key.
    // Authored nodes/recipes stay unchanged; selected results retain their keys.
    iCAX::TemplateRuntime::SNeutralModel RigidPrototypeGraph(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const std::vector<std::string>& Roots_,std::size_t& Reused_)
    {
        using iCAX::TemplateRuntime::CStandardJsonCodec;
        iCAX::TemplateRuntime::SNeutralModel _Model;
        std::unordered_map<std::string,const SGeometryNode*> _Sources;
        for(const auto& _Node:Model_.Geometry)
            if(!_Sources.emplace(_Node.Key,&_Node).second)
                throw std::invalid_argument("duplicate neutral model geometry key: "+_Node.Key);
        struct SLocal final { std::string Key;gp_Trsf Placement; };
        std::unordered_map<std::string,SLocal> _Resolved;
        std::unordered_set<std::string> _Resolving;
        std::map<std::string,std::string> _Identities;
        const bool _ProfileBooleanNodes = GetEnvironmentVariableA("ICAX_PROFILE_NEUTRAL_BOOLEAN_NODES", nullptr, 0) > 0;
        std::size_t _NextKey=0;
        const auto _Add=[&](EGeometryOperator Operator_,std::vector<std::string> Inputs_,ObjectMap Arguments_) {
            VariantArray _Inputs;
            for(const auto& _Input:Inputs_)_Inputs.emplace_back(_Input);
            ObjectMap _IdentityDocument;
            _IdentityDocument.emplace("operator",static_cast<unsigned int>(Operator_));
            _IdentityDocument.emplace("inputs",std::move(_Inputs));
            _IdentityDocument.emplace("arguments",std::move(Arguments_));
            const auto _Identity=CStandardJsonCodec::Serialize(_IdentityDocument);
            if(const auto _Found=_Identities.find(_Identity);_Found!=_Identities.end()) {
                ++Reused_;return _Found->second;
            }
            std::string _Key;
            do {_Key="runtime.prototype."+std::to_string(_NextKey++);}while(_Sources.contains(_Key));
            _Model.Geometry.push_back({_Key,Operator_,std::move(Inputs_),
                std::move(std::get<ObjectMap>(_IdentityDocument.at("arguments").m_Value))});
            _Identities.emplace(_Identity,_Key);
            return _Key;
        };
        const auto _Place=[&](const SLocal& Local_,const gp_Trsf& ToFrame_) {
            const auto _Transform=ToFrame_*Local_.Placement;
            if(IsExactIdentityTransform(_Transform))return Local_.Key;
            return _Add(EGeometryOperator::Transform,{Local_.Key},RigidPlacementArguments(_Transform));
        };
        const std::function<SLocal(const std::string&)> _Resolve=[&](const std::string& Key_)->SLocal {
            if(const auto _Found=_Resolved.find(Key_);_Found!=_Resolved.end())return _Found->second;
            const auto _Found=_Sources.find(Key_);
            if(_Found==_Sources.end())throw std::invalid_argument("neutral model references missing geometry: "+Key_);
            if(!_Resolving.emplace(Key_).second)throw std::invalid_argument("neutral model geometry dependency cycle: "+Key_);
            const auto& _Node=*_Found->second;
            const auto _Path="geometry."+_Node.Key;
            auto _Arguments=_Node.Arguments;
            SLocal _Local;
            switch(_Node.Operator) {
            case EGeometryOperator::Profile2D: {
                const auto _Plane=Placement(_Arguments,_Path);
                const gp_Dir _Normal(gp_Vec(_Plane.XAxis).Crossed(gp_Vec(_Plane.YAxis)));
                _Local.Placement.SetDisplacement(gp_Ax3(),gp_Ax3(_Plane.Origin,_Normal,_Plane.XAxis));
                auto _Placement=RequireObject(Require(_Arguments,"placement",_Path),_Path+".placement");
                _Placement["origin"]=VariantArray{0.,0.,0.};
                _Placement["xAxis"]=VariantArray{1.,0.,0.};
                _Placement["yAxis"]=VariantArray{0.,1.,0.};
                _Arguments["placement"]=std::move(_Placement);
                _Local.Key=_Add(_Node.Operator,{},std::move(_Arguments));
                break;
            }
            case EGeometryOperator::Resource:
                if(!_Node.Inputs.empty())throw std::invalid_argument(_Path+" resource must not have inputs");
                _Local.Key=_Add(_Node.Operator,{},std::move(_Arguments));
                break;
            case EGeometryOperator::Extrude: {
                if(_Node.Inputs.size()!=1)throw std::invalid_argument(_Path+" extrude requires exactly one input");
                const auto _Profile=_Resolve(_Node.Inputs.front());
                auto _Vector=Vector3(Require(_Arguments,"vector",_Path),_Path+".vector");
                _Vector.Transform(_Profile.Placement.Inverted());
                _Arguments["vector"]=VariantArray{_Vector.X()==0.?0.:_Vector.X(),
                    _Vector.Y()==0.?0.:_Vector.Y(),_Vector.Z()==0.?0.:_Vector.Z()};
                _Local.Key=_Add(_Node.Operator,{_Profile.Key},std::move(_Arguments));
                _Local.Placement=_Profile.Placement;
                break;
            }
            case EGeometryOperator::Transform: {
                if(_Node.Inputs.size()!=1)throw std::invalid_argument(_Path+" transform requires exactly one input");
                _Local=_Resolve(_Node.Inputs.front());
                const auto _Placement=RigidNodeTransform(_Node)*_Local.Placement;
                if(IsPlainRigidTransform(_Node))_Local.Placement=_Placement;
                else {
                    // An accepted but unrecognized argument is an execution
                    // boundary. Retain it and the complete transform node;
                    // factor only the already known input pose into its frame.
                    auto& _CompletePlacement=std::get<ObjectMap>(_Arguments.at("placement").m_Value);
                    const auto _KnownPlacement=RigidPlacementArguments(_Placement);
                    for(const auto& [_Name,_Value]:std::get<ObjectMap>(_KnownPlacement.at("placement").m_Value))
                        _CompletePlacement[_Name]=_Value;
                    _Local.Key=_Add(_Node.Operator,{_Local.Key},std::move(_Arguments));
                    _Local.Placement=gp_Trsf();
                }
                break;
            }
            case EGeometryOperator::Boolean: {
                const auto _Inputs=BooleanInputs(_Node);
                const auto _Target=_Resolve(_Inputs.front());
                const auto _ToFrame=_Target.Placement.Inverted();
                std::vector<std::string> _LocalInputs{_Target.Key};
                VariantArray _Tools;
                for(auto _Input=_Inputs.begin()+1;_Input!=_Inputs.end();++_Input) {
                    const auto _Tool=_Place(_Resolve(*_Input),_ToFrame);
                    _LocalInputs.push_back(_Tool);_Tools.emplace_back(_Tool);
                }
                _Arguments["target"]=_Target.Key;_Arguments["tools"]=std::move(_Tools);
                if(const auto _Witness=Find(_Arguments,"keepConnectedTo")) {
                    auto _Point=Point3(*_Witness,_Path+".keepConnectedTo");_Point.Transform(_ToFrame);
                    _Arguments["keepConnectedTo"]=VariantArray{_Point.X(),_Point.Y(),_Point.Z()};
                }
                _Local.Key=_Add(_Node.Operator,std::move(_LocalInputs),std::move(_Arguments));
                if (_ProfileBooleanNodes)
                    std::fprintf(stderr,"Disassembly/neutral-boolean-source key=%s prototype=%s operation=%s\n",
                        _Node.Key.c_str(),_Local.Key.c_str(),RequireString(_Node.Arguments,"operation",_Path).c_str());
                _Local.Placement=_Target.Placement;
                break;
            }
            case EGeometryOperator::Compound: {
                if(_Node.Inputs.empty())throw std::invalid_argument(_Path+" compound requires inputs");
                _Local.Placement=_Resolve(_Node.Inputs.front()).Placement;
                const auto _ToFrame=_Local.Placement.Inverted();
                std::vector<std::string> _Inputs;
                for(const auto& _Key:_Node.Inputs)_Inputs.push_back(_Place(_Resolve(_Key),_ToFrame));
                _Local.Key=_Add(_Node.Operator,std::move(_Inputs),std::move(_Arguments));
                break;
            }
            default:
                throw std::invalid_argument(_Path+" uses an operator not implemented by the OpenCascade adapter");
            }
            _Resolving.erase(Key_);_Resolved.emplace(Key_,_Local);
            return _Local;
        };
        std::unordered_set<std::string> _AddedRoots;
        for(const auto& _Root:Roots_)if(_AddedRoots.emplace(_Root).second) {
            const auto _Local=_Resolve(_Root);
            _Model.Geometry.push_back({_Root,EGeometryOperator::Transform,{_Local.Key},RigidPlacementArguments(_Local.Placement)});
        }
        return _Model;
    }
}

namespace {
void ValidateSolidShape(
    const TopoDS_Shape& Shape_, const std::string& Context_, bool FullValidation_)
{
    if (Shape_.IsNull()) throw std::invalid_argument(Context_ + " requires a non-empty solid BRep");
    if (FullValidation_ && !BRepCheck_Analyzer(Shape_).IsValid())
        throw std::invalid_argument(Context_ + " contains invalid BRep topology");

    std::size_t _SolidCount = 0;
    const std::function<void(const TopoDS_Shape&, unsigned int)> _ValidateSolidTree =
        [&](const TopoDS_Shape& Shape_, unsigned int Depth_) {
            if (Depth_ > 64)
                throw std::invalid_argument(Context_ + " exceeds the maximum compound nesting depth");
            if (Shape_.ShapeType() == TopAbs_SOLID)
            {
                if (!FullValidation_) {
                    for (TopExp_Explorer _Shell(Shape_, TopAbs_SHELL); _Shell.More(); _Shell.Next())
                        if (!BRep_Tool::IsClosed(_Shell.Current()))
                            throw std::invalid_argument(Context_ + " requires closed solid shells");
                }
                GProp_GProps _Mass;
                BRepGProp::VolumeProperties(Shape_, _Mass);
                if (!std::isfinite(_Mass.Mass()) || _Mass.Mass() <= 0.0)
                    throw std::invalid_argument(Context_ + " requires finite positive-volume solids");
                ++_SolidCount;
                return;
            }
            if (Shape_.ShapeType() != TopAbs_COMPOUND && Shape_.ShapeType() != TopAbs_COMPSOLID)
                throw std::invalid_argument(Context_ + " only accepts solids or compounds of solids, not loose faces, edges or shells");
            const auto _PreviousCount = _SolidCount;
            for (TopoDS_Iterator _Child(Shape_); _Child.More(); _Child.Next())
                _ValidateSolidTree(_Child.Value(), Depth_ + 1);
            if (_SolidCount == _PreviousCount)
                throw std::invalid_argument(Context_ + " contains an empty compound");
        };
    _ValidateSolidTree(Shape_, 0);
    Bnd_Box _Bounds;
    BRepBndLib::Add(Shape_, _Bounds, false);
    if (_Bounds.IsVoid() || _Bounds.IsOpen())
        throw std::invalid_argument(Context_ + " requires finite non-empty bounds");
    std::array<double, 6> _Extents;
    _Bounds.Get(_Extents[0], _Extents[1], _Extents[2], _Extents[3], _Extents[4], _Extents[5]);
    if (!std::all_of(_Extents.begin(), _Extents.end(), [](double Value_) { return std::isfinite(Value_); }))
        throw std::invalid_argument(Context_ + " requires finite non-empty bounds");
}
}

void iCAX::OpenCascade::ValidateSolidResourceShape(
    const TopoDS_Shape& Shape_, const std::string& Context_)
{
    ValidateSolidShape(Shape_, Context_, true);
}

void iCAX::OpenCascade::ValidateSolidPreviewShape(
    const TopoDS_Shape& Shape_, const std::string& Context_)
{
    ValidateSolidShape(Shape_, Context_, false);
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
    return EvaluateNeutralModel(Model_, GeometryKeys_, SNeutralModelEvaluationOptions{});
}

iCAX::OpenCascade::SNeutralModelEvaluation iCAX::OpenCascade::EvaluateNeutralModel(
    const SNeutralModel& Model_,
    const std::vector<std::string>& GeometryKeys_,
    const SNeutralModelEvaluationOptions& Options_)
{
    SNeutralModel _ExecutionGraph;
    const SNeutralModel* _Graph=&Model_;
    std::size_t _RigidReuse=0;
    if(Options_.ReuseRigidPrototypes) {
        _ExecutionGraph=RigidPrototypeGraph(Model_,GeometryKeys_,_RigidReuse);
        _Graph=&_ExecutionGraph;
    }
    std::unordered_map<std::string, const SGeometryNode*> _Nodes;
    for (const auto& _Node : _Graph->Geometry)
        if (!_Nodes.emplace(_Node.Key, &_Node).second)
            throw std::invalid_argument("duplicate neutral model geometry key: " + _Node.Key);

    // A - B - C is exactly A - (B union C). Collapse only unobserved,
    // single-consumer subtraction intermediates without selection arguments.
    // In particular, keepConnectedTo is an observable per-step material
    // selection, so it always remains a boundary. This changes neither the
    // authored graph nor the frozen operation recipe returned by the host.
    std::unordered_set<std::string> _SelectedRoots(GeometryKeys_.begin(), GeometryKeys_.end());
    std::unordered_set<std::string> _Reachable;
    std::unordered_map<std::string, std::size_t> _Consumers;
    const std::function<void(const std::string&)> _Collect = [&](const std::string& Key_) {
        if (!_Reachable.emplace(Key_).second) return;
        const auto _Found = _Nodes.find(Key_);
        if (_Found == _Nodes.end()) throw std::invalid_argument("neutral model references missing geometry: " + Key_);
        const auto& _Node = *_Found->second;
        auto _Dependencies = _Node.Operator == EGeometryOperator::Boolean ? BooleanInputs(_Node) : _Node.Inputs;
        if (_Node.Operator == EGeometryOperator::Profile2D || _Node.Operator == EGeometryOperator::Resource)
            _Dependencies.clear();
        std::unordered_set<std::string> _Unique;
        for (const auto& _Key : _Dependencies) if (_Unique.emplace(_Key).second) {
            ++_Consumers[_Key];
            _Collect(_Key);
        }
    };
    if (Options_.CombineUnobservedSubtractions || Options_.IntersectMatchingExtrusions
        || Options_.ExpandSubtractionToolUnions)
        for (const auto& _Root : GeometryKeys_) _Collect(_Root);
    const auto _SimpleBoolean = [&](const SGeometryNode& Node_, const char* Operation_) {
        if (Node_.Operator != EGeometryOperator::Boolean
            || RequireString(Node_.Arguments, "operation", "geometry." + Node_.Key) != Operation_) return false;
        return std::all_of(Node_.Arguments.begin(), Node_.Arguments.end(), [](const auto& Field_) {
            return Field_.first == "operation" || Field_.first == "target" || Field_.first == "tools";
        });
    };
    const auto _SimpleSubtraction = [&](const SGeometryNode& Node_) { return _SimpleBoolean(Node_, "subtract"); };
    const auto _Unobserved = [&](const std::string& Key_) {
        const auto _Found = _Consumers.find(Key_);
        return !_SelectedRoots.contains(Key_) && _Found != _Consumers.end() && _Found->second == 1;
    };
    std::deque<SGeometryNode> _CombinedNodes;
    std::size_t _CombinedSubtractions = 0;
    std::size_t _PlanarIntersections = 0, _ExpandedToolUnions = 0, _NextExecutionKey = 0;
    const auto _AddExecutionNode = [&](EGeometryOperator Operator_, std::vector<std::string> Inputs_, ObjectMap Arguments_) {
        std::string _Key;
        do { _Key = "runtime.exact-operation." + std::to_string(_NextExecutionKey++); } while (_Nodes.contains(_Key));
        _CombinedNodes.push_back({_Key, Operator_, std::move(Inputs_), std::move(Arguments_)});
        _Nodes.emplace(_Key, &_CombinedNodes.back());
        return _Key;
    };

    struct SUnionOperand final { std::string Key; gp_Trsf Placement; };
    std::unordered_set<std::string> _ExpandingUnions;
    const std::function<bool(const std::string&, const gp_Trsf&, std::vector<SUnionOperand>&)> _ExpandUnion =
        [&](const std::string& Key_, const gp_Trsf& Placement_, std::vector<SUnionOperand>& Operands_) -> bool {
        const auto _Found = _Nodes.find(Key_);
        if (_Found == _Nodes.end()) throw std::invalid_argument("neutral model references missing geometry: " + Key_);
        if (!_ExpandingUnions.emplace(Key_).second)
            throw std::invalid_argument("neutral model geometry dependency cycle at: " + Key_);
        const auto& _Node = *_Found->second;
        bool _Expanded = false;
        if (_Unobserved(Key_) && _SimpleBoolean(_Node, "union")) {
            for (const auto& _Child : BooleanInputs(_Node)) (void)_ExpandUnion(_Child, Placement_, Operands_);
            ++_ExpandedToolUnions;
            _Expanded = true;
        } else if (_Unobserved(Key_) && _Node.Inputs.size() == 1 && IsPlainRigidTransform(_Node)) {
            std::vector<SUnionOperand> _Children;
            _Expanded = _ExpandUnion(_Node.Inputs.front(), Placement_ * RigidNodeTransform(_Node), _Children);
            if (_Expanded) Operands_.insert(Operands_.end(),
                std::make_move_iterator(_Children.begin()), std::make_move_iterator(_Children.end()));
        }
        if (!_Expanded) Operands_.push_back({Key_, Placement_});
        _ExpandingUnions.erase(Key_);
        return _Expanded;
    };

    // Plan the selected subgraph before launching work. In particular, Boolean
    // arguments may override Inputs; unused declarations must not be evaluated.
    struct SPlannedNode
    {
        const SGeometryNode* Node;
        std::vector<std::string> Inputs;
    };
    std::vector<std::vector<SPlannedNode>> _Levels;
    std::unordered_map<std::string, std::size_t> _Depths;
    std::unordered_set<std::string> _Visiting;
    std::function<std::size_t(const std::string&)> _Plan;
    _Plan = [&](const std::string& strKey_) -> std::size_t {
        if (const auto _Existing = _Depths.find(strKey_); _Existing != _Depths.end())
            return _Existing->second;
        const auto _NodeIterator = _Nodes.find(strKey_);
        if (_NodeIterator == _Nodes.end())
            throw std::invalid_argument("neutral model references missing geometry: " + strKey_);
        if (!_Visiting.emplace(strKey_).second)
            throw std::invalid_argument("neutral model geometry dependency cycle: " + strKey_);
        const auto* _EffectiveNode = _NodeIterator->second;
        if (Options_.IntersectMatchingExtrusions && _SimpleBoolean(*_EffectiveNode, "intersect")) {
            const auto _Inputs = BooleanInputs(*_EffectiveNode);
            if (_Inputs.size() == 2 && _Unobserved(_Inputs[0]) && _Unobserved(_Inputs[1])) {
                const auto _A = _Nodes.find(_Inputs[0]), _B = _Nodes.find(_Inputs[1]);
                if (_A != _Nodes.end() && _B != _Nodes.end()
                    && _A->second->Operator == EGeometryOperator::Extrude && _B->second->Operator == EGeometryOperator::Extrude
                    && _A->second->Inputs.size() == 1 && _B->second->Inputs.size() == 1
                    && _A->second->Arguments == _B->second->Arguments
                    && std::all_of(_A->second->Arguments.begin(), _A->second->Arguments.end(), [](const auto& Field_) {
                        return Field_.first == "vector" || Field_.first == "extendStart" || Field_.first == "extendEnd";
                    })) {
                    const auto* _ExtrudeA = _A->second;
                    const auto _ProfileA = _Nodes.find(_ExtrudeA->Inputs.front()), _ProfileB = _Nodes.find(_B->second->Inputs.front());
                    if (_ProfileA != _Nodes.end() && _ProfileB != _Nodes.end()
                        && _ProfileA->second->Operator == EGeometryOperator::Profile2D
                        && _ProfileB->second->Operator == EGeometryOperator::Profile2D
                        && std::all_of(_ProfileA->second->Arguments.begin(), _ProfileA->second->Arguments.end(), [](const auto& Field_) {
                            return Field_.first == "placement" || Field_.first == "contours";
                        })
                        && std::all_of(_ProfileB->second->Arguments.begin(), _ProfileB->second->Arguments.end(), [](const auto& Field_) {
                            return Field_.first == "placement" || Field_.first == "contours";
                        })) {
                        const auto& _PlaneA = Require(_ProfileA->second->Arguments, "placement", "geometry." + _ProfileA->first);
                        const auto& _PlaneB = Require(_ProfileB->second->Arguments, "placement", "geometry." + _ProfileB->first);
                        const auto _Plane = Placement(_ProfileA->second->Arguments, "geometry." + _ProfileA->first);
                        const auto _Vector = Vector3(Require(_ExtrudeA->Arguments,"vector","geometry."+_ExtrudeA->Key),"geometry."+_ExtrudeA->Key+".vector");
                        const auto& _PlacementFields=RequireObject(_PlaneA,"geometry."+_ProfileA->first+".placement");
                        // The common planar frame and non-tangential vector
                        // give every solid point a unique section coordinate:
                        // E(A,v) intersect E(B,v) = E(A intersect B,v).
                        if (_PlaneA == _PlaneB
                            && std::all_of(_PlacementFields.begin(), _PlacementFields.end(), [](const auto& Field_) {
                                return Field_.first == "origin" || Field_.first == "xAxis" || Field_.first == "yAxis";
                            })
                            && std::abs(_Vector.Dot(gp_Vec(_Plane.XAxis).Crossed(gp_Vec(_Plane.YAxis)))) > kTolerance) {
                            const auto _SectionKey = _AddExecutionNode(EGeometryOperator::Boolean,
                                {_ProfileA->first, _ProfileB->first}, ObjectMap{{"operation", std::string("intersect")}});
                            _CombinedNodes.push_back({strKey_, EGeometryOperator::Extrude, {_SectionKey}, _ExtrudeA->Arguments});
                            _EffectiveNode = &_CombinedNodes.back();
                            ++_PlanarIntersections;
                        }
                    }
                }
            }
        }
        if (Options_.CombineUnobservedSubtractions && _SimpleSubtraction(*_EffectiveNode)) {
            auto _CombinedInputs = BooleanInputs(*_EffectiveNode);
            std::unordered_set<std::string> _Chain{strKey_};
            while (true) {
                const auto& _Target = _CombinedInputs.front();
                if (_SelectedRoots.contains(_Target) || _Consumers.at(_Target) != 1) break;
                const auto _Previous = _Nodes.find(_Target);
                if (_Previous == _Nodes.end() || !_SimpleSubtraction(*_Previous->second)) break;
                if (!_Chain.emplace(_Target).second)
                    throw std::invalid_argument("neutral model geometry dependency cycle at: " + _Target);
                auto _PreviousInputs = BooleanInputs(*_Previous->second);
                _PreviousInputs.insert(_PreviousInputs.end(), _CombinedInputs.begin() + 1, _CombinedInputs.end());
                _CombinedInputs = std::move(_PreviousInputs);
                ++_CombinedSubtractions;
            }
            if (_CombinedInputs != BooleanInputs(*_EffectiveNode)) {
                _CombinedNodes.push_back(*_EffectiveNode);
                auto& _Combined = _CombinedNodes.back();
                _Combined.Inputs = _CombinedInputs;
                _Combined.Arguments["target"] = _CombinedInputs.front();
                VariantArray _Tools;
                for (auto _Tool = _CombinedInputs.begin() + 1; _Tool != _CombinedInputs.end(); ++_Tool) _Tools.emplace_back(*_Tool);
                _Combined.Arguments["tools"] = std::move(_Tools);
                _EffectiveNode = &_Combined;
            }
        }
        if (Options_.ExpandSubtractionToolUnions && _SimpleSubtraction(*_EffectiveNode)) {
            const auto _OriginalInputs = BooleanInputs(*_EffectiveNode);
            std::vector<std::string> _ExpandedInputs{_OriginalInputs.front()};
            bool _Expanded = false;
            for (auto _Tool = _OriginalInputs.begin() + 1; _Tool != _OriginalInputs.end(); ++_Tool) {
                std::vector<SUnionOperand> _Operands;
                const auto _DidExpand = _ExpandUnion(*_Tool, gp_Trsf(), _Operands);
                _Expanded = _Expanded || _DidExpand;
                for (auto& _Operand : _Operands)
                    _ExpandedInputs.push_back(IsExactIdentityTransform(_Operand.Placement) ? _Operand.Key
                        : _AddExecutionNode(EGeometryOperator::Transform, {_Operand.Key}, RigidPlacementArguments(_Operand.Placement)));
            }
            if (_Expanded) {
                _CombinedNodes.push_back(*_EffectiveNode);
                auto& _Combined = _CombinedNodes.back();
                _Combined.Inputs = _ExpandedInputs;
                _Combined.Arguments["target"] = _ExpandedInputs.front();
                VariantArray _Tools;
                for (auto _Tool = _ExpandedInputs.begin() + 1; _Tool != _ExpandedInputs.end(); ++_Tool) _Tools.emplace_back(*_Tool);
                _Combined.Arguments["tools"] = std::move(_Tools);
                _EffectiveNode = &_Combined;
            }
        }
        const auto& _Node = *_EffectiveNode;
        auto _Inputs = _Node.Operator == EGeometryOperator::Boolean ? BooleanInputs(_Node) : _Node.Inputs;
        if (_Node.Operator == EGeometryOperator::Profile2D || _Node.Operator == EGeometryOperator::Resource)
            _Inputs.clear(); // Their builders validate their own input contract.
        std::size_t _Depth = 0;
        for (const auto& _Input : _Inputs) _Depth = std::max(_Depth, _Plan(_Input) + 1);
        if (_Levels.size() <= _Depth) _Levels.resize(_Depth + 1);
        _Levels[_Depth].push_back({ &_Node, std::move(_Inputs) });
        _Visiting.erase(strKey_);
        _Depths.emplace(strKey_, _Depth);
        return _Depth;
    };
    for (const auto& _GeometryKey : GeometryKeys_) (void)_Plan(_GeometryKey);

    SNeutralModelEvaluation _Result;
    const bool _Profile = GetEnvironmentVariableA("ICAX_PROFILE_DISASSEMBLY", nullptr, 0) > 0;
    const bool _ProfileBooleanNodes = GetEnvironmentVariableA("ICAX_PROFILE_NEUTRAL_BOOLEAN_NODES", nullptr, 0) > 0;
    const auto _Started = std::chrono::steady_clock::now();
    constexpr auto _OperatorCount = static_cast<std::size_t>(EGeometryOperator::Resource) + 1;
    std::array<std::atomic<unsigned long long>, _OperatorCount> _BuildMicroseconds{};
    std::array<std::atomic<std::size_t>, _OperatorCount> _BuildCounts{};
    unsigned long long _CopyMicroseconds = 0;
    const auto _Concurrency = detail::GeometryConcurrency(Options_.MaximumConcurrency);
    const auto _Build = [&](const SGeometryNode& _Node,
        const std::function<const TopoDS_Shape&(const std::string&)>& _Evaluate) {
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        const auto _BuildStarted = std::chrono::steady_clock::now();
        TopoDS_Shape _Shape;
        switch (_Node.Operator)
        {
        case EGeometryOperator::Profile2D:
            _Shape = MakeProfile2D(_Node);
            break;
        case EGeometryOperator::Resource:
            _Shape = MakeResource(_Node);
            break;
        case EGeometryOperator::Extrude:
            if (_Node.Inputs.size() != 1)
                throw std::invalid_argument("geometry." + _Node.Key + " extrude requires exactly one input");
            _Shape = MakeExtrude(_Node, _Evaluate(_Node.Inputs.front()));
            break;
        case EGeometryOperator::Boolean:
            _Shape = BooleanShape(_Node, _Evaluate, Options_.UseBoundingBoxFilter,
                Options_.SuppressUnusedBooleanHistory, Options_.UseOrientedBoundingBoxes);
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
        if (_ProfileBooleanNodes && _Node.Operator == EGeometryOperator::Boolean)
            std::fprintf(stderr,"Disassembly/neutral-boolean-node key=%s operation=%s inputs=%zu elapsed_ms=%.6f\n",
                _Node.Key.c_str(),RequireString(_Node.Arguments,"operation","geometry."+_Node.Key).c_str(),
                BooleanInputs(_Node).size(),std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-_BuildStarted).count());
        if (_Profile) {
            const auto _Index = static_cast<std::size_t>(_Node.Operator);
            if (_Index < _BuildMicroseconds.size()) {
                _BuildMicroseconds[_Index].fetch_add(static_cast<unsigned long long>(
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - _BuildStarted).count()),
                    std::memory_order_relaxed);
                _BuildCounts[_Index].fetch_add(1, std::memory_order_relaxed);
            }
        }
        return _Shape;
    };
    // The levels above validate the graph; they are not execution barriers.
    // Publish each completed node immediately and release its own dependants.
    std::vector<const SPlannedNode*> _PlannedNodes;
    std::unordered_map<std::string, std::size_t> _NodeIndices;
    for (const auto& _Level : _Levels)
        for (const auto& _Planned : _Level)
        {
            _NodeIndices.emplace(_Planned.Node->Key, _PlannedNodes.size());
            _PlannedNodes.push_back(&_Planned);
        }
    const auto _NodeCount = _PlannedNodes.size();
    std::vector<std::size_t> _Pending(_NodeCount);
    std::vector<std::vector<std::size_t>> _Dependants(_NodeCount);
    std::deque<std::size_t> _Ready, _InlineReady;
    const auto _Enqueue = [&](std::size_t Index_) {
        const auto _Operator = _PlannedNodes[Index_]->Node->Operator;
        (_Operator == EGeometryOperator::Transform || _Operator == EGeometryOperator::Compound
            ? _InlineReady : _Ready).push_back(Index_);
    };
    for (std::size_t _Index = 0; _Index < _NodeCount; ++_Index)
    {
        std::unordered_set<std::string> _UniqueInputs;
        for (const auto& _Key : _PlannedNodes[_Index]->Inputs)
            if (_UniqueInputs.emplace(_Key).second)
            {
                ++_Pending[_Index];
                _Dependants[_NodeIndices.at(_Key)].push_back(_Index);
            }
        if (_Pending[_Index] == 0) _Enqueue(_Index);
    }
    std::size_t _Published = 0, _InFlight = 0;
    const auto _Publish = [&](std::size_t Index_, TopoDS_Shape Shape_) {
        const auto& _Key = _PlannedNodes[Index_]->Node->Key;
        const auto _Entry = _Result.Geometry.emplace(_Key, std::move(Shape_));
        ++_Published;
        for (const auto _Dependant : _Dependants[Index_])
            if (--_Pending[_Dependant] == 0) _Enqueue(_Dependant);
        if (Options_.OnSelectedRootReady && _SelectedRoots.contains(_Key))
            Options_.OnSelectedRootReady(_Key, _Entry.first->second);
    };
    std::vector<TopoDS_Shape> _Shapes(_NodeCount);
    std::vector<std::exception_ptr> _Errors(_NodeCount);
    std::vector<std::size_t> _TaskIndices(_NodeCount), _Completions, _Finished;
    _Completions.reserve(_NodeCount);
    _Finished.reserve(_NodeCount);
    std::mutex _CompletionMutex;
    std::condition_variable _CompletionReady;
    // Declared after all worker state: exception unwinding must join every task
    // before its captures (including the completion queue) can be destroyed.
    SGeometryTaskBatch _Tasks;
    _Tasks.Tasks.reserve(_NodeCount);
    while (_Published < _NodeCount)
    {
        COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        {
            const std::lock_guard _Lock(_CompletionMutex);
            _Finished.swap(_Completions);
        }
        for (const auto _Index : _Finished)
        {
            _Tasks.Tasks[_TaskIndices[_Index]].Result();
            --_InFlight;
            if (_Errors[_Index]) std::rethrow_exception(_Errors[_Index]);
            _Publish(_Index, std::move(_Shapes[_Index]));
        }
        _Finished.clear();
        if (!_InlineReady.empty())
        {
            const auto _Index = _InlineReady.front();
            _InlineReady.pop_front();
            // Only the coordinator touches shared TShapes. Workers use private
            // copies, so even Compound's topology flag updates are isolated.
            _Publish(_Index, _Build(*_PlannedNodes[_Index]->Node,
                [&](const std::string& Key_) -> const TopoDS_Shape& { return _Result.At(Key_); }));
            continue;
        }
        if (!_Ready.empty() && _InFlight < _Concurrency)
        {
            const auto _Index = _Ready.front();
            _Ready.pop_front();
            if (_Concurrency == 1 || (_InFlight == 0 && _Ready.empty()))
            {
                // A lone ready node cannot overlap other work. Retain cheap
                // serial execution and no-op builders' shared-shape identity.
                _Publish(_Index, _Build(*_PlannedNodes[_Index]->Node,
                    [&](const std::string& Key_) -> const TopoDS_Shape& { return _Result.At(Key_); }));
                continue;
            }
            std::map<std::string, TopoDS_Shape> _Inputs;
            const auto& _Planned = *_PlannedNodes[_Index];
            const auto _CopyStarted = std::chrono::steady_clock::now();
            for (const auto& _Key : _Planned.Inputs)
                if (!_Inputs.contains(_Key))
                    _Inputs.emplace(_Key, BRepBuilderAPI_Copy(_Result.At(_Key), true, false).Shape());
            if (_Profile) _CopyMicroseconds += static_cast<unsigned long long>(
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - _CopyStarted).count());
            _TaskIndices[_Index] = _Tasks.Tasks.size();
            _Tasks.Tasks.push_back(iCAX::Tasks::Run([&, _Index, _Inputs = std::move(_Inputs),
                _CancellationCheck = COpenCascadeCancellationScope::Capture()] {
                try
                {
                    COpenCascadeCancellationScope _Cancellation(_CancellationCheck);
                    COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                    _Shapes[_Index] = _Build(*_PlannedNodes[_Index]->Node,
                        [&](const std::string& Key_) -> const TopoDS_Shape& { return _Inputs.at(Key_); });
                }
                catch (...) { _Errors[_Index] = std::current_exception(); }
                {
                    const std::lock_guard _Lock(_CompletionMutex);
                    _Completions.push_back(_Index); // Both queue buffers are preallocated.
                }
                _CompletionReady.notify_one();
            }, GeometryTaskScheduler()));
            ++_InFlight;
            continue;
        }
        if (_Published == _NodeCount) break;
        if (_InFlight == 0) throw std::logic_error("neutral model scheduler has no ready work");
        std::unique_lock _Lock(_CompletionMutex);
        _CompletionReady.wait(_Lock, [&] { return !_Completions.empty(); });
    }
    if (_Profile) {
        std::fprintf(stderr, "Disassembly/neutral-evaluator nodes=%zu combined=%zu rigidReused=%zu planarIntersections=%zu expandedToolUnions=%zu copy=%.3f total=%.3f ms",
            _NodeCount, _CombinedSubtractions, _RigidReuse, _PlanarIntersections, _ExpandedToolUnions, static_cast<double>(_CopyMicroseconds) / 1000,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _Started).count());
        for (std::size_t _Index = 0; _Index < _BuildCounts.size(); ++_Index)
            std::fprintf(stderr, " op%zu=%zu/%.3f", _Index, _BuildCounts[_Index].load(),
                static_cast<double>(_BuildMicroseconds[_Index].load()) / 1000);
        std::fprintf(stderr, "\n");
    }
    return _Result;
}
