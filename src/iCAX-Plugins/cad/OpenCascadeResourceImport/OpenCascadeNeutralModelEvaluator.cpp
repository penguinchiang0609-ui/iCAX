#include "pch.h"

#include "OpenCascadeNeutralModelEvaluator.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
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

    TopoDS_Face MakePolygonFace(
        const SPlacement& Placement_, const std::vector<std::pair<double, double>>& Points_,
        const std::string& strPath_)
    {
        BRepBuilderAPI_MakePolygon _Polygon;
        for (const auto& [_X, _Y] : Points_) _Polygon.Add(LocalPoint(Placement_, _X, _Y));
        _Polygon.Close();
        if (!_Polygon.IsDone()) throw std::runtime_error(strPath_ + " failed to build a closed polygon");
        BRepBuilderAPI_MakeFace _Face(_Polygon.Wire(), true);
        if (!_Face.IsDone()) throw std::runtime_error(strPath_ + " failed to build a face");
        return _Face.Face();
    }

    TopoDS_Face MakeProfile2D(const SGeometryNode& Node_)
    {
        const auto _Path = std::string("geometry.") + Node_.Key;
        const auto _Placement = Placement(Node_.Arguments, _Path);
        const auto& _Contour = RequireObject(
            Require(Node_.Arguments, "contour", _Path), _Path + ".contour");
        const auto _Kind = RequireString(_Contour, "kind", _Path + ".contour");
        if (_Kind == "circle")
        {
            const auto _Radius = Number(Require(_Contour, "radius", _Path + ".contour"), _Path + ".contour.radius");
            if (_Radius <= kTolerance) throw std::invalid_argument(_Path + ".contour.radius must be positive");
            const gp_Dir _Normal(gp_Vec(_Placement.XAxis).Crossed(gp_Vec(_Placement.YAxis)));
            BRepBuilderAPI_MakeEdge _Edge(gp_Circ(gp_Ax2(_Placement.Origin, _Normal, _Placement.XAxis), _Radius));
            if (!_Edge.IsDone()) throw std::runtime_error(_Path + " failed to build a circle");
            BRepBuilderAPI_MakeWire _Wire(_Edge.Edge());
            if (!_Wire.IsDone()) throw std::runtime_error(_Path + " failed to build a circular wire");
            BRepBuilderAPI_MakeFace _Face(_Wire.Wire(), true);
            if (!_Face.IsDone()) throw std::runtime_error(_Path + " failed to build a circular face");
            return _Face.Face();
        }
        if (_Kind == "polygon")
        {
            const auto& _PointValues = RequireArray(
                Require(_Contour, "points", _Path + ".contour"),
                _Path + ".contour.points");
            if (_PointValues.size() < 3)
                throw std::invalid_argument(_Path + ".contour.points requires at least three points");
            std::vector<std::pair<double, double>> _Points;
            _Points.reserve(_PointValues.size());
            for (std::size_t _Index = 0; _Index < _PointValues.size(); ++_Index)
                _Points.push_back(Point2(_PointValues[_Index],
                    _Path + ".contour.points[" + std::to_string(_Index) + "]"));
            return MakePolygonFace(_Placement, _Points, _Path);
        }
        if (_Kind != "roundedRectangle")
            throw std::invalid_argument(_Path + ".contour.kind is unsupported: " + _Kind);

        const auto _Width = Number(Require(_Contour, "width", _Path + ".contour"), _Path + ".contour.width");
        const auto _Height = Number(Require(_Contour, "height", _Path + ".contour"), _Path + ".contour.height");
        const auto _Radius = OptionalNumber(_Contour, "radius", 0.0, _Path + ".contour");
        if (_Width <= kTolerance || _Height <= kTolerance || _Radius < 0.0
            || _Radius >= std::min(_Width, _Height) / 2.0)
        {
            throw std::invalid_argument(_Path + ".contour rounded rectangle dimensions are invalid");
        }
        const auto _HalfWidth = _Width / 2.0;
        const auto _HalfHeight = _Height / 2.0;
        if (_Radius <= kTolerance)
        {
            return MakePolygonFace(_Placement, {
                { -_HalfWidth, -_HalfHeight }, { _HalfWidth, -_HalfHeight },
                { _HalfWidth, _HalfHeight }, { -_HalfWidth, _HalfHeight }
            }, _Path);
        }

        const auto _Diagonal = _Radius / std::sqrt(2.0);
        const auto _Point = [&](double X_, double Y_) { return LocalPoint(_Placement, X_, Y_); };
        const auto _BottomLeft = _Point(-_HalfWidth + _Radius, -_HalfHeight);
        const auto _BottomRight = _Point(_HalfWidth - _Radius, -_HalfHeight);
        const auto _RightBottom = _Point(_HalfWidth, -_HalfHeight + _Radius);
        const auto _RightTop = _Point(_HalfWidth, _HalfHeight - _Radius);
        const auto _TopRight = _Point(_HalfWidth - _Radius, _HalfHeight);
        const auto _TopLeft = _Point(-_HalfWidth + _Radius, _HalfHeight);
        const auto _LeftTop = _Point(-_HalfWidth, _HalfHeight - _Radius);
        const auto _LeftBottom = _Point(-_HalfWidth, -_HalfHeight + _Radius);
        BRepBuilderAPI_MakeWire _Wire;
        _Wire.Add(Line(_BottomLeft, _BottomRight, _Path));
        _Wire.Add(Arc(_BottomRight, _Point(_HalfWidth - _Radius + _Diagonal, -_HalfHeight + _Radius - _Diagonal), _RightBottom, _Path));
        _Wire.Add(Line(_RightBottom, _RightTop, _Path));
        _Wire.Add(Arc(_RightTop, _Point(_HalfWidth - _Radius + _Diagonal, _HalfHeight - _Radius + _Diagonal), _TopRight, _Path));
        _Wire.Add(Line(_TopRight, _TopLeft, _Path));
        _Wire.Add(Arc(_TopLeft, _Point(-_HalfWidth + _Radius - _Diagonal, _HalfHeight - _Radius + _Diagonal), _LeftTop, _Path));
        _Wire.Add(Line(_LeftTop, _LeftBottom, _Path));
        _Wire.Add(Arc(_LeftBottom, _Point(-_HalfWidth + _Radius - _Diagonal, -_HalfHeight + _Radius - _Diagonal), _BottomLeft, _Path));
        if (!_Wire.IsDone()) throw std::runtime_error(_Path + " failed to build a rounded rectangle wire");
        BRepBuilderAPI_MakeFace _Face(_Wire.Wire(), true);
        if (!_Face.IsDone()) throw std::runtime_error(_Path + " failed to build a rounded rectangle face");
        return _Face.Face();
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
            if (_Operation == "subtract")
            {
                BRepAlgoAPI_Cut _Builder(_Result, _Tool);
                if (!_Builder.IsDone()) throw std::runtime_error(_Path + " subtraction failed");
                _Result = _Builder.Shape();
            }
            else if (_Operation == "union")
            {
                BRepAlgoAPI_Fuse _Builder(_Result, _Tool);
                if (!_Builder.IsDone()) throw std::runtime_error(_Path + " union failed");
                _Result = _Builder.Shape();
            }
            else if (_Operation == "intersect")
            {
                BRepAlgoAPI_Common _Builder(_Result, _Tool);
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
    std::unordered_map<std::string, const SGeometryNode*> _Nodes;
    for (const auto& _Node : Model_.Geometry)
        if (!_Nodes.emplace(_Node.Key, &_Node).second)
            throw std::invalid_argument("duplicate neutral model geometry key: " + _Node.Key);

    SNeutralModelEvaluation _Result;
    std::function<const TopoDS_Shape&(const std::string&)> _Evaluate;
    _Evaluate = [&](const std::string& strKey_) -> const TopoDS_Shape& {
        if (const auto _Existing = _Result.Geometry.find(strKey_); _Existing != _Result.Geometry.end())
            return _Existing->second;
        const auto _NodeIterator = _Nodes.find(strKey_);
        if (_NodeIterator == _Nodes.end())
            throw std::invalid_argument("neutral model references missing geometry: " + strKey_);
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
        return _Result.Geometry.emplace(_Node.Key, std::move(_Shape)).first->second;
    };
    for (const auto& _Node : Model_.Geometry) (void)_Evaluate(_Node.Key);
    return _Result;
}
