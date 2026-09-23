#include "pch.h"
#include "ExtrusionRecognitionService.h"
#include "SectionGeometry.h"
#include "ExtrudeRecognizesService.h"

#include "TemplateRuntime/PythonTemplateHost.h"
#include "TemplateRuntime/StandardJsonCodec.h"

#include <filesystem>
#include <fstream>

namespace iCAX::ExtrusionRecognition
{
namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    using iCAX::GeometryData::BRepModel;
    using iCAX::GeometryData::Direction2;
    using iCAX::GeometryData::Direction3;
    using iCAX::GeometryData::Point2;
    using iCAX::GeometryData::Transform3;

    constexpr double kPi = 3.14159265358979323846;

    std::optional<double> Number(IN const Variant& Value_)
    {
        if (Value_.Is<bool>()) return std::nullopt;
        return std::visit([](IN const auto& Value_) -> std::optional<double> {
            using T = std::decay_t<decltype(Value_)>;
            if constexpr (std::is_arithmetic_v<T>)
            {
                const auto _Value = static_cast<double>(Value_);
                return std::isfinite(_Value) ? std::optional<double>(_Value) : std::nullopt;
            }
            return std::nullopt;
        }, Value_.m_Value);
    }

    Variant PointValue(IN const Point2& Point_)
    {
        return Variant(VariantArray{
            Variant(Point_.X),
            Variant(Point_.Y) });
    }

    Variant EdgeValue(IN const SSectionWireEdge& Edge_)
    {
        using namespace iCAX::GeometryData;
        ObjectMap _Result{
            { "sourceEdgeId", static_cast<unsigned long long>(Edge_.SourceEdgeId) },
            { "start", PointValue(Edge_.Start) },
            { "end", PointValue(Edge_.End) },
            { "reversed", Edge_.bReversed },
            { "first", Edge_.Range.First },
            { "last", Edge_.Range.Last } };
        std::visit([&](const auto& Curve_) {
            using T = std::decay_t<decltype(Curve_)>;
            if constexpr (std::is_same_v<T, Line2> || std::is_same_v<T, Ray2>
                || std::is_same_v<T, Segment2>)
            {
                _Result["kind"] = std::string("line");
            }
            else if constexpr (std::is_same_v<T, Circle2> || std::is_same_v<T, Arc2>
                || std::is_same_v<T, Ellipse2> || std::is_same_v<T, EllipseArc2>)
            {
                const auto& _Basis = [&]() -> const auto& {
                    if constexpr (std::is_same_v<T, Arc2> || std::is_same_v<T, EllipseArc2>)
                        return Curve_.Basis;
                    else return Curve_;
                }();
                double _First = 0.0, _Sweep = 2.0 * kPi;
                if constexpr (std::is_same_v<T, Arc2> || std::is_same_v<T, EllipseArc2>)
                {
                    _First = Curve_.StartAngle;
                    _Sweep = Curve_.CounterClockwise
                        ? Curve_.EndAngle - Curve_.StartAngle : Curve_.StartAngle - Curve_.EndAngle;
                }
                if (Edge_.bReversed) { _First += _Sweep; _Sweep = -_Sweep; }
                const auto& _X = _Basis.Placement.XDirection;
                const auto& _Y = _Basis.Placement.YDirection;
                const auto _Handedness = _X.X * _Y.Y - _X.Y * _Y.X < 0.0 ? -1.0 : 1.0;
                const auto _Rotation = std::atan2(_X.Y, _X.X);
                _First *= _Handedness;
                _Sweep *= _Handedness;
                _Result["center"] = PointValue(_Basis.Placement.Location);
                if constexpr (std::is_same_v<T, Circle2> || std::is_same_v<T, Arc2>)
                {
                    _Result["kind"] = std::string("circleArc");
                    _Result["radius"] = _Basis.Radius;
                    _Result["first"] = _Rotation + _First;
                    _Result["last"] = _Rotation + _First + _Sweep;
                }
                else
                {
                    _Result["kind"] = std::string("ellipseArc");
                    _Result["majorRadius"] = _Basis.MajorRadius;
                    _Result["minorRadius"] = _Basis.MinorRadius;
                    _Result["rotation"] = _Rotation;
                    _Result["startAngle"] = _First;
                    _Result["endAngle"] = _First + _Sweep;
                }
            }
            else if constexpr (std::is_same_v<T, Polyline2>)
            {
                _Result["kind"] = std::string("polyline");
                VariantArray _Points;
                for (const auto& _Point : Curve_.Points) _Points.push_back(PointValue(_Point));
                _Result["points"] = std::move(_Points);
                _Result["closed"] = Curve_.Closed;
            }
            else if constexpr (std::is_same_v<T, Bezier2> || std::is_same_v<T, BSpline2>
                || std::is_same_v<T, NURBS2>)
            {
                // Preserve exact data even for curve types whose inverse is
                // unsupported. Never substitute sampled lines for a spline.
                VariantArray _Poles;
                for (const auto& _Point : Curve_.Poles) _Poles.push_back(PointValue(_Point));
                _Result["poles"] = std::move(_Poles);
                if constexpr (std::is_same_v<T, Bezier2>) _Result["kind"] = std::string("bezier");
                else
                {
                    _Result["kind"] = std::string(std::is_same_v<T, NURBS2> ? "nurbs" : "bspline");
                    _Result["degree"] = Curve_.Degree;
                    _Result["periodic"] = Curve_.Periodic;
                    VariantArray _Knots, _Multiplicities;
                    for (auto _Value : Curve_.Knots) _Knots.emplace_back(_Value);
                    for (auto _Value : Curve_.Multiplicities) _Multiplicities.emplace_back(_Value);
                    _Result["knots"] = std::move(_Knots);
                    _Result["multiplicities"] = std::move(_Multiplicities);
                    if constexpr (std::is_same_v<T, NURBS2>)
                    {
                        VariantArray _Weights;
                        for (auto _Value : Curve_.Weights) _Weights.emplace_back(_Value);
                        _Result["weights"] = std::move(_Weights);
                    }
                }
            }
            else
            {
                _Result["kind"] = std::string("clothoid");
                _Result["origin"] = PointValue(Curve_.Placement.Location);
                _Result["rotation"] = std::atan2(Curve_.Placement.XDirection.Y, Curve_.Placement.XDirection.X);
                _Result["startCurvature"] = Curve_.StartCurvature;
                _Result["endCurvature"] = Curve_.EndCurvature;
                _Result["length"] = Curve_.Length;
            }
        }, Edge_.Curve);
        return _Result;
    }

    VariantArray MakeContourArray(IN const std::vector<SSectionWire>& Wires_)
    {
        VariantArray _Contours;
        _Contours.reserve(Wires_.size());
        for (std::size_t _Index = 0; _Index < Wires_.size(); ++_Index)
        {
            const auto& _Wire = Wires_[_Index];
            VariantArray _Edges;
            _Edges.reserve(_Wire.Edges.size());
            for (const auto& _Edge : _Wire.Edges)
            {
                _Edges.push_back(EdgeValue(_Edge));
            }
            _Contours.push_back(Variant(ObjectMap{
                { "id", _Index == 0 ? std::string("outer")
                    : "inner-" + std::to_string(_Index) },
                { "inner", _Wire.bInner },
                { "closed", _Wire.bClosed },
                { "kind", std::string("path") },
                { "signedArea", _Wire.dSignedArea },
                { "edges", std::move(_Edges) }
            }));
        }
        return _Contours;
    }

    bool TryReadPoint(IN const Variant& Value_, OUT Point2& Point_)
    {
        if (!Value_.Is<VariantArray>() || Value_.To<VariantArray>().size() != 2)
        {
            return false;
        }
        const auto& _Array = Value_.To<VariantArray>();
        const auto _X = Number(_Array[0]);
        const auto _Y = Number(_Array[1]);
        if (!_X || !_Y) return false;
        Point_ = { *_X, *_Y };
        return true;
    }

    bool TryReadFittingPose(IN const ObjectMap& Candidate_, OUT Transform3& Transform_)
    {
        const auto _Angle = Candidate_.find("rotationDegrees");
        const auto _Translation = Candidate_.find("translation");
        Point2 _Origin;
        if (_Angle == Candidate_.end() || _Translation == Candidate_.end()
            || !TryReadPoint(_Translation->second, _Origin)) return false;
        const auto _Degrees = Number(_Angle->second);
        if (!_Degrees) return false;
        const auto _Radians = *_Degrees * kPi / 180.0;
        const auto _C = std::cos(_Radians), _S = std::sin(_Radians);
        // fitting reports canonical -> input. Apply its inverse in the YZ
        // section plane to normalize the X-aligned solid, preserving X.
        Transform_.Matrix.Values = {{
            { 1.0, 0.0, 0.0, 0.0 },
            { 0.0, _C, _S, -_C * _Origin.X - _S * _Origin.Y },
            { 0.0, -_S, _C, _S * _Origin.X - _C * _Origin.Y },
            { 0.0, 0.0, 0.0, 1.0 } }};
        return true;
    }

    std::filesystem::path CanonicalFile(
        IN const std::string& Text_, IN const char* pDescription_)
    {
        if (Text_.empty()) return {};
        const std::filesystem::path _Path(Text_);
        std::error_code _Error;
        if (!std::filesystem::is_regular_file(_Path, _Error))
        {
            throw std::runtime_error(
                std::string(pDescription_) + " was not found: " + Text_);
        }
        return std::filesystem::weakly_canonical(_Path, _Error);
    }

    std::vector<std::filesystem::path> SearchRoots()
    {
        std::vector<std::filesystem::path> _Roots;
        std::error_code _Error;
        auto _Root = std::filesystem::absolute(std::filesystem::current_path(), _Error);
        if (_Error) return _Roots;
        for (;; _Root = _Root.parent_path())
        {
            _Roots.push_back(_Root);
            if (_Root == _Root.parent_path()) break;
        }
        return _Roots;
    }

    std::filesystem::path ResolveRuntimeFile(
        IN const std::string& ExplicitPath_,
        IN const std::vector<std::string>& RelativeCandidates_,
        IN const char* pDescription_)
    {
        if (!ExplicitPath_.empty())
        {
            return CanonicalFile(ExplicitPath_, pDescription_);
        }
        std::error_code _Error;
        for (const auto& _Root : SearchRoots())
        {
            for (const auto& _Relative : RelativeCandidates_)
            {
                const auto _Candidate = _Root / _Relative;
                if (std::filesystem::is_regular_file(_Candidate, _Error))
                {
                    return std::filesystem::weakly_canonical(_Candidate, _Error);
                }
                _Error.clear();
            }
        }
        throw std::runtime_error(
            std::string(pDescription_) + " was not found in the application installation");
    }

    iCAX::TemplateRuntime::CPythonTemplateHost& PythonHost(
        IN const SPythonSectionFitterOptions& Options_)
    {
        static std::mutex _Mutex;
        static std::unique_ptr<iCAX::TemplateRuntime::CPythonTemplateHost> _Host;
        static std::filesystem::path _Runtime;
        static std::filesystem::path _Worker;
        static std::unique_lock<std::mutex> _Lock(_Mutex, std::defer_lock);
        _Lock.lock();
        const auto _RuntimePath = ResolveRuntimeFile(
            Options_.PythonRuntimeLibraryPath,
            {
                "src/x64/Debug/runtime/python/python312.dll",
                "src/x64/Release/runtime/python/python312.dll",
                "x64/Debug/runtime/python/python312.dll",
                "x64/Release/runtime/python/python312.dll"
            },
            "embedded Python runtime library");
        const auto _WorkerPath = ResolveRuntimeFile(
            Options_.WorkerScriptPath,
            {
                "src/x64/Debug/runtime/template-python/icax_template_worker.py",
                "src/x64/Release/runtime/template-python/icax_template_worker.py",
                "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py"
            },
            "embedded Python fitter worker");
        if (!_Host || _Runtime != _RuntimePath || _Worker != _WorkerPath)
        {
            _Host = std::make_unique<iCAX::TemplateRuntime::CPythonTemplateHost>(
                iCAX::TemplateRuntime::SPythonTemplateHostOptions{
                    _RuntimePath,
                    _WorkerPath,
                    _WorkerPath.parent_path()
                });
            _Runtime = _RuntimePath;
            _Worker = _WorkerPath;
        }
        _Lock.unlock();
        return *_Host;
    }

    Transform3 Multiply(IN const Transform3& Left_, IN const Transform3& Right_)
    {
        Transform3 _Result;
        for (std::size_t _Row = 0; _Row < 4; ++_Row)
        {
            for (std::size_t _Column = 0; _Column < 4; ++_Column)
            {
                _Result.Matrix.Values[_Row][_Column] = 0.0;
                for (std::size_t _Index = 0; _Index < 4; ++_Index)
                {
                    _Result.Matrix.Values[_Row][_Column] +=
                        Left_.Matrix.Values[_Row][_Index]
                        * Right_.Matrix.Values[_Index][_Column];
                }
            }
        }
        return _Result;
    }

    std::vector<Point2> ReadLoopPoints(IN const SSectionWire& Wire_)
    {
        std::vector<Point2> _Points;
        for (const auto& _Edge : Wire_.Edges)
        {
            if (_Edge.Samples.empty())
            {
                if (_Points.empty()) _Points.push_back(_Edge.Start);
                _Points.push_back(_Edge.End);
                continue;
            }
            const auto _Begin = _Points.empty() ? 0u : 1u;
            _Points.insert(
                _Points.end(),
                _Edge.Samples.begin() + _Begin,
                _Edge.Samples.end());
        }
        if (_Points.size() > 1
            && std::hypot(
                _Points.front().X - _Points.back().X,
                _Points.front().Y - _Points.back().Y) <= 1.0e-9)
        {
            _Points.pop_back();
        }
        return _Points;
    }

    double SignedArea(IN const std::vector<Point2>& Points_)
    {
        double _Area = 0.0;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _A = Points_[_Index];
            const auto& _B = Points_[(_Index + 1) % Points_.size()];
            _Area += _A.X * _B.Y - _B.X * _A.Y;
        }
        return 0.5 * _Area;
    }

    Point2 PolygonCentroid(IN const std::vector<Point2>& Points_)
    {
        const auto _Area = SignedArea(Points_);
        if (std::abs(_Area) <= 1.0e-12)
        {
            Point2 _Result;
            for (const auto& _Point : Points_)
            {
                _Result.X += _Point.X;
                _Result.Y += _Point.Y;
            }
            if (!Points_.empty())
            {
                _Result.X /= static_cast<double>(Points_.size());
                _Result.Y /= static_cast<double>(Points_.size());
            }
            return _Result;
        }
        Point2 _Result;
        for (std::size_t _Index = 0; _Index < Points_.size(); ++_Index)
        {
            const auto& _A = Points_[_Index];
            const auto& _B = Points_[(_Index + 1) % Points_.size()];
            const auto _Cross = _A.X * _B.Y - _B.X * _A.Y;
            _Result.X += (_A.X + _B.X) * _Cross;
            _Result.Y += (_A.Y + _B.Y) * _Cross;
        }
        _Result.X /= 6.0 * _Area;
        _Result.Y /= 6.0 * _Area;
        return _Result;
    }

    iCAX::GeometryData::BoundingBox2 SectionBounds(
        IN const std::vector<SSectionWire>& Wires_,
        IN const Transform3& Transform_ = {})
    {
        using namespace iCAX::GeometryData;
        BoundingBox2 _Bounds;
        bool _FirstPoint = true;
        const auto& _M = Transform_.Matrix.Values;
        const auto _Move = [&](const Point2& P_) -> Point2 {
            return { _M[1][1] * P_.X + _M[1][2] * P_.Y + _M[1][3],
                _M[2][1] * P_.X + _M[2][2] * P_.Y + _M[2][3] };
        };
        const auto _Add = [&](const Point2& P_) {
            if (_FirstPoint) { _Bounds.Min = _Bounds.Max = P_; _FirstPoint = false; }
            else
            {
                _Bounds.Min.X = std::min(_Bounds.Min.X, P_.X); _Bounds.Min.Y = std::min(_Bounds.Min.Y, P_.Y);
                _Bounds.Max.X = std::max(_Bounds.Max.X, P_.X); _Bounds.Max.Y = std::max(_Bounds.Max.Y, P_.Y);
            }
        };
        for (const auto& _Wire : Wires_)
            for (const auto& _Edge : _Wire.Edges)
            {
                _Add(_Move(_Edge.Start)); _Add(_Move(_Edge.End));
                for (const auto& _P : _Edge.Samples) _Add(_Move(_P));
                std::visit([&](const auto& Curve_) {
                    using T = std::decay_t<decltype(Curve_)>;
                    if constexpr (std::is_same_v<T, Circle2> || std::is_same_v<T, Arc2>
                        || std::is_same_v<T, Ellipse2> || std::is_same_v<T, EllipseArc2>)
                    {
                        const auto& _Basis = [&]() -> const auto& {
                            if constexpr (std::is_same_v<T, Arc2> || std::is_same_v<T, EllipseArc2>) return Curve_.Basis;
                            else return Curve_;
                        }();
                        double _A, _B;
                        if constexpr (std::is_same_v<T, Circle2> || std::is_same_v<T, Arc2>) _A = _B = _Basis.Radius;
                        else { _A = _Basis.MajorRadius; _B = _Basis.MinorRadius; }
                        double _Start = 0.0, _Sweep = 2.0 * kPi;
                        if constexpr (std::is_same_v<T, Arc2> || std::is_same_v<T, EllipseArc2>)
                        {
                            _Start = Curve_.StartAngle;
                            _Sweep = Curve_.CounterClockwise ? Curve_.EndAngle - Curve_.StartAngle
                                : Curve_.StartAngle - Curve_.EndAngle;
                        }
                        const auto _Center = _Move(_Basis.Placement.Location);
                        const auto& _X = _Basis.Placement.XDirection; const auto& _Y = _Basis.Placement.YDirection;
                        const Point2 _Cos{ _A * (_M[1][1] * _X.X + _M[1][2] * _X.Y),
                            _A * (_M[2][1] * _X.X + _M[2][2] * _X.Y) };
                        const Point2 _Sin{ _B * (_M[1][1] * _Y.X + _M[1][2] * _Y.Y),
                            _B * (_M[2][1] * _Y.X + _M[2][2] * _Y.Y) };
                        const auto _Low = std::min(_Start, _Start + _Sweep), _High = std::max(_Start, _Start + _Sweep);
                        for (const auto _Angle : { std::atan2(_Sin.X, _Cos.X), std::atan2(_Sin.Y, _Cos.Y) })
                            for (const auto _Critical : { _Angle, _Angle + kPi })
                            {
                                const auto _InRange = _Critical + 2.0 * kPi * std::ceil((_Low - _Critical - 1e-12) / (2.0 * kPi));
                                if (_InRange <= _High + 1e-12)
                                    _Add({ _Center.X + _Cos.X * std::cos(_InRange) + _Sin.X * std::sin(_InRange),
                                        _Center.Y + _Cos.Y * std::cos(_InRange) + _Sin.Y * std::sin(_InRange) });
                            }
                    }
                }, _Edge.Curve);
            }
        _Bounds.Empty = _FirstPoint;
        return _Bounds;
    }

    SSectionSnapshot MakeSnapshot(IN const std::vector<SSectionWire>& Wires_)
    {
        SSectionSnapshot _Result;
        double _SignedMaterialArea = 0.0;
        Point2 _Minimum;
        Point2 _Maximum;
        bool _HasPoint = false;
        for (std::size_t _Index = 0; _Index < Wires_.size(); ++_Index)
        {
            const auto _Points = ReadLoopPoints(Wires_[_Index]);
            if (_Points.empty()) continue;
            SSectionLoop2 _Loop;
            _Loop.ID = _Index == 0 ? "outer" : "inner-" + std::to_string(_Index);
            _Loop.bInner = _Index != 0;
            _Loop.Points = _Points;
            _Result.Loops.push_back(std::move(_Loop));
            const auto _Area = SignedArea(_Points);
            const auto _Centroid = PolygonCentroid(_Points);
            const auto _Weight = _Index == 0 ? 1.0 : -1.0;
            _SignedMaterialArea += _Weight * std::abs(_Area);
            _Result.Centroid.X += _Weight * std::abs(_Area) * _Centroid.X;
            _Result.Centroid.Y += _Weight * std::abs(_Area) * _Centroid.Y;
            for (const auto& _Point : _Points)
            {
                if (!_HasPoint)
                {
                    _Minimum = _Maximum = _Point;
                    _HasPoint = true;
                }
                else
                {
                    _Minimum.X = std::min(_Minimum.X, _Point.X);
                    _Minimum.Y = std::min(_Minimum.Y, _Point.Y);
                    _Maximum.X = std::max(_Maximum.X, _Point.X);
                    _Maximum.Y = std::max(_Maximum.Y, _Point.Y);
                }
            }
        }
        _Result.dMaterialArea = std::max(0.0, _SignedMaterialArea);
        if (_Result.dMaterialArea > 1.0e-12)
        {
            _Result.Centroid.X /= _Result.dMaterialArea;
            _Result.Centroid.Y /= _Result.dMaterialArea;
        }
        if (_HasPoint)
        {
            _Result.Bounds = SectionBounds(Wires_);
            const auto _DX = _Result.Bounds.Max.X - _Result.Bounds.Min.X;
            const auto _DY = _Result.Bounds.Max.Y - _Result.Bounds.Min.Y;
            _Result.PrincipalLongAxis = _DX >= _DY
                ? Direction2{ 1.0, 0.0 }
                : Direction2{ 0.0, 1.0 };
            _Result.PrincipalShortAxis = {
                -_Result.PrincipalLongAxis.Y,
                _Result.PrincipalLongAxis.X
            };
        }
        _Result.nCavityCount = _Result.Loops.size() > 0
            ? _Result.Loops.size() - 1
            : 0;
        return _Result;
    }

    void TransformSnapshot(IN OUT SSectionSnapshot& Section_, IN const Transform3& Transform_)
    {
        const auto& _M = Transform_.Matrix.Values;
        const auto _Move = [&](const Point2& Point_) -> Point2 {
            return { _M[1][1] * Point_.X + _M[1][2] * Point_.Y + _M[1][3],
                _M[2][1] * Point_.X + _M[2][2] * Point_.Y + _M[2][3] };
        };
        bool _First = true;
        for (auto& _Loop : Section_.Loops)
        {
            for (auto& _Point : _Loop.Points)
            {
                _Point = _Move(_Point);
                if (_First) { Section_.Bounds.Min = Section_.Bounds.Max = _Point; _First = false; }
                else
                {
                    Section_.Bounds.Min.X = std::min(Section_.Bounds.Min.X, _Point.X);
                    Section_.Bounds.Min.Y = std::min(Section_.Bounds.Min.Y, _Point.Y);
                    Section_.Bounds.Max.X = std::max(Section_.Bounds.Max.X, _Point.X);
                    Section_.Bounds.Max.Y = std::max(Section_.Bounds.Max.Y, _Point.Y);
                }
            }
        }
        Section_.Centroid = _Move(Section_.Centroid);
        for (auto* _Axis : { &Section_.PrincipalLongAxis, &Section_.PrincipalShortAxis })
            *_Axis = { _M[1][1] * _Axis->X + _M[1][2] * _Axis->Y,
                _M[2][1] * _Axis->X + _M[2][2] * _Axis->Y };
    }

    double AxialExtent(IN const BRepModel& Geometry_, IN const Direction3& Direction_)
    {
        double _Minimum = (std::numeric_limits<double>::max)();
        double _Maximum = (std::numeric_limits<double>::lowest)();
        bool _HasPoint = false;
        const auto _Visit = [&](IN const auto& Point_) {
            const auto _Station = Point_.X * Direction_.X
                + Point_.Y * Direction_.Y
                + Point_.Z * Direction_.Z;
            _Minimum = std::min(_Minimum, _Station);
            _Maximum = std::max(_Maximum, _Station);
            _HasPoint = true;
        };
        for (const auto& _Vertex : Geometry_.Vertices) _Visit(_Vertex.Position);
        for (const auto& _Mesh : Geometry_.Triangulations3)
            for (const auto& _Point : _Mesh.Geometry.Vertices) _Visit(_Point);
        return _HasPoint ? std::max(0.0, _Maximum - _Minimum) : 0.0;
    }

    bool TryReadFitterResult(
        IN const ObjectMap& Response_,
        OUT SSectionMatchResult& Match_,
        OUT std::string& strError_)
    {
        const auto _Matched = Response_.find("matched");
        if (_Matched == Response_.end() || !_Matched->second.Is<bool>())
        {
            strError_ = "Python fitter result must contain boolean matched";
            return false;
        }
        const auto _Results = Response_.find("results");
        if (_Results == Response_.end() || !_Results->second.Is<VariantArray>()
            || _Results->second.To<VariantArray>().size() != 1)
        {
            strError_ = "Direct fitting must return exactly one template result";
            return false;
        }
        const auto _Item = _Results->second.To<VariantArray>().front().To<ObjectMap>();
        const auto _Status = _Item.at("status").To<std::string>();
        const auto _Reason = _Item.contains("reason") ? ": " + _Item.at("reason").To<std::string>() : "";
        if (_Status == "error") { strError_ = _Status + _Reason; return false; }
        Match_.bMatched = _Matched->second.To<bool>();
        if (!Match_.bMatched)
        {
            if (_Status != "no-match" && _Status != "unsupported" && _Status != "unsupported-geometry")
            { strError_ = "Unexpected direct fitting status: " + _Status; return false; }
            Match_.Diagnostics.push_back(_Status + _Reason);
            return true;
        }
        const auto _Candidates = _Item.at("candidates").To<VariantArray>();
        if (_Status != "matched" || _Candidates.size() != 1)
        { strError_ = "Matched direct fitting must return one parameter/pose candidate"; return false; }
        const auto _Candidate = _Candidates.front().To<ObjectMap>();
        if (!_Candidate.contains("parameters") || !_Candidate.at("parameters").Is<ObjectMap>()
            || !TryReadFittingPose(_Candidate, Match_.TRSF))
        { strError_ = "Direct fitting must return parameters, rotationDegrees and translation"; return false; }
        Match_.Parameters = _Candidate.at("parameters").To<ObjectMap>();
        Match_.Placement.Origin = {};
        Match_.Placement.XDirection = { 1.0, 0.0 };
        Match_.Placement.ZDirection = { 0.0, 1.0 };
        if (const auto _Diagnostics = _Candidate.find("diagnostics");
            _Diagnostics != _Candidate.end()
            && _Diagnostics->second.Is<VariantArray>())
        {
            for (const auto& _Item : _Diagnostics->second.To<VariantArray>())
            {
                if (_Item.Is<std::string>()) Match_.Diagnostics.push_back(_Item.To<std::string>());
            }
        }
        return true;
    }
}

std::vector<SPythonSectionFitter> DiscoverPythonSectionFitters(
    IN const std::string& ProfileRoot_)
{
    std::vector<SPythonSectionFitter> _Result;
    std::unordered_map<std::string,double> _Priorities;
    std::error_code _Error;
    const std::filesystem::path _Root(ProfileRoot_);
    if (!std::filesystem::is_directory(_Root, _Error)) return _Result;
    for (std::filesystem::directory_iterator _Iterator(_Root, _Error), _End;
        !_Error && _Iterator != _End;
        _Iterator.increment(_Error))
    {
        if (!_Iterator->is_directory(_Error))
        {
            _Error.clear();
            continue;
        }
        const auto _Script = _Iterator->path() / "fitting.py";
        const auto _Manifest = _Iterator->path() / "profile.json";
        if (!std::filesystem::is_regular_file(_Manifest, _Error))
        {
            _Error.clear();
            continue;
        }
        _Result.push_back({
            _Iterator->path().filename().string(),
            std::filesystem::weakly_canonical(_Script, _Error).string(),
            {} });
        if(std::filesystem::is_regular_file(_Manifest)) {
            std::ifstream _Stream(_Manifest);
            const std::string _Text((std::istreambuf_iterator<char>(_Stream)),std::istreambuf_iterator<char>());
            const auto _Descriptor=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_Text).To<ObjectMap>();
            if(const auto _It=_Descriptor.find("fitterPriority");_It!=_Descriptor.end()) {
                const auto _Priority=Number(_It->second);
                if(!_Priority||_It->second.Is<bool>())throw std::invalid_argument("fitterPriority must be finite numeric data");
                _Priorities[_Result.back().TypeID]=*_Priority;
            }
            std::ifstream _ScriptStream(_Script);
            const std::string _Code((std::istreambuf_iterator<char>(_ScriptStream)),std::istreambuf_iterator<char>());
            std::uint64_t _Hash=14695981039346656037ull;
            for(const unsigned char _Byte:_Text+"\n"+_Code){_Hash^=_Byte;_Hash*=1099511628211ull;}
            _Result.back().PackageDigest=std::to_string(_Hash);
        }
        _Error.clear();
    }
    std::sort(_Result.begin(), _Result.end(), [&](const auto& Left_, const auto& Right_) {
        const auto _Left=_Priorities[Left_.TypeID],_Right=_Priorities[Right_.TypeID];
        if(_Left!=_Right)return _Left>_Right;
        return Left_.TypeID < Right_.TypeID;
    });
    return _Result;
}

SRecognitionResult CExtrusionRecognitionService::RecognizePythonFitters(
    IN const BRepModel& Geometry_,
    IN std::span<const SPythonSectionFitter> OrderedFitters_,
    IN const SPythonSectionFitterOptions& FitterOptions_)
{
    SRecognitionResult _Result;
    if (OrderedFitters_.empty() || !std::isfinite(FitterOptions_.dLinearTolerance)
        || FitterOptions_.dLinearTolerance <= 0.0 || FitterOptions_.dLinearTolerance > 1.0)
    {
        _Result.Status = ERecognitionStatus::InvalidRequest;
        _Result.Diagnostics.push_back("Python section fitter list or tolerance is invalid");
        return _Result;
    }

    CExtrudeRecognizesService _DirectionService;
    const auto _Direction = _DirectionService.Recognize(Geometry_);
    if (!_Direction.bSuccess)
    {
        _Result.Status = ERecognitionStatus::AxisNotFound;
        _Result.Diagnostics = _Direction.Diagnostics;
        return _Result;
    }
    _Result.Direction = _Direction.Direction;
    _Result.Diagnostics.insert(
        _Result.Diagnostics.end(),
        _Direction.Diagnostics.begin(),
        _Direction.Diagnostics.end());

    const auto _Sections = _DirectionService.ExtractSectionWires(
        _Direction.AlignedGeometry);
    if (!_Sections.bSuccess)
    {
        _Result.Status = ERecognitionStatus::SectionExtractionFailed;
        _Result.Diagnostics.insert(
            _Result.Diagnostics.end(),
            _Sections.Diagnostics.begin(),
            _Sections.Diagnostics.end());
        return _Result;
    }
    _Result.Section = MakeSnapshot(_Sections.Wires);
    _Result.dLength = AxialExtent(Geometry_, _Direction.Direction);
    // Even when every type-specific fitter rejects the contour, the part is
    // still a valid linear extrusion.  Keep the direction-normalized result
    // so callers can persist an "irregular" tube without running another
    // geometry recognition pass.
    _Result.NormalizedGeometry = _Direction.AlignedGeometry;
    _Result.TRSF = _Direction.TRSF;

    const auto _Contours = MakeContourArray(_Sections.Wires);
    bool _AnyCompleted = false;
    for (const auto& _Fitter : OrderedFitters_)
    {
        if (_Fitter.TypeID.empty() || _Fitter.ScriptPath.empty())
        {
            _Result.Diagnostics.push_back("Skipped Python fitter with empty type id or script path");
            continue;
        }
        try
        {
            const auto _Script = std::filesystem::path(_Fitter.ScriptPath);
            const auto _ProfileRoot = _Script.parent_path().parent_path();
            const auto _AdjacentRuntime = _ProfileRoot.parent_path() / "_shared" / "profile_package_runtime.py";
            const auto _Runtime = ResolveRuntimeFile(
                std::filesystem::is_regular_file(_AdjacentRuntime) ? _AdjacentRuntime.string() : "",
                { "apps/tube-designer/templates/_shared/profile_package_runtime.py",
                  "src/apps/tube-designer/templates/_shared/profile_package_runtime.py" },
                "direct profile fitting runtime");
            ObjectMap _Request{
                { "protocol", std::string("icax.template-runtime") },
                { "protocolVersion", static_cast<unsigned int>(1) },
                { "operation", std::string("evaluate") },
                { "templatePath", _Runtime.string() },
                { "template", ObjectMap{ { "packageDigest", _Fitter.PackageDigest } } },
                { "parameters", ObjectMap{
                    { "action", std::string("recognize-system") },
                    { "profileRoot", _ProfileRoot.string() },
                    { "profileId", _Fitter.TypeID },
                    { "section", ObjectMap{ { "contours", _Contours } } },
                    { "tolerance", FitterOptions_.dLinearTolerance } } },
                { "context", ObjectMap{} }
            };
            // This runtime dispatches fitting.py with preview=False. No
            // profile.py/build or generated candidate geometry is executed.
            const auto _Response = PythonHost(FitterOptions_).Invoke(_Request);
            SSectionMatchResult _Match;
            std::string _Error;
            if (!TryReadFitterResult(_Response, _Match, _Error))
            {
                if (!FitterOptions_.bContinueAfterFitterError)
                {
                    _Result.Status = ERecognitionStatus::SectionTypeDefinitionInvalid;
                    _Result.Diagnostics.push_back(_Fitter.TypeID + ": " + _Error);
                    return _Result;
                }
                _Result.Diagnostics.push_back(_Fitter.TypeID + ": " + _Error);
                continue;
            }
            _AnyCompleted = true;
            if (!_Match.bMatched)
            {
                for (const auto& _Diagnostic : _Match.Diagnostics)
                    _Result.Diagnostics.push_back(_Fitter.TypeID + ": " + _Diagnostic);
                continue;
            }

            std::string _TransformError;
            BRepModel _FittedGeometry;
            if (!TryApplyBRepTransform(
                    _Direction.AlignedGeometry,
                    _Match.TRSF,
                    _FittedGeometry,
                    _TransformError))
            {
                _Result.Status = ERecognitionStatus::PlacementFailed;
                _Result.Diagnostics.push_back(
                    _Fitter.TypeID + ": failed to apply fitter trsf: " + _TransformError);
                return _Result;
            }
            _Result.Status = ERecognitionStatus::Success;
            _Result.SectionTypeID = _Fitter.TypeID;
            _Result.SectionParameters = std::move(_Match.Parameters);
            _Result.NormalizedGeometry = std::move(_FittedGeometry);
            TransformSnapshot(_Result.Section, _Match.TRSF);
            // Recompute analytic extrema in the fitted frame. Rotating a
            // sampled circle/ellipse polygon underestimates its envelope.
            _Result.Section.Bounds = SectionBounds(_Sections.Wires, _Match.TRSF);
            _Result.TRSF = Multiply(_Match.TRSF, _Direction.TRSF);
            _Result.Diagnostics.insert(
                _Result.Diagnostics.end(),
                _Match.Diagnostics.begin(),
                _Match.Diagnostics.end());
            _Result.Diagnostics.push_back(
                "Python section fitter matched in supplied order: " + _Fitter.TypeID);
            return _Result;
        }
        catch (const std::exception& Error_)
        {
            if (!FitterOptions_.bContinueAfterFitterError)
            {
                _Result.Status = ERecognitionStatus::SectionTypeDefinitionInvalid;
                _Result.Diagnostics.push_back(_Fitter.TypeID + ": " + Error_.what());
                return _Result;
            }
            _Result.Diagnostics.push_back(_Fitter.TypeID + ": " + Error_.what());
        }
    }
    _Result.Status = _AnyCompleted ? ERecognitionStatus::SectionTypeNotMatched
        : ERecognitionStatus::SectionTypeDefinitionInvalid;
    _Result.Diagnostics.push_back("No Python section fitter matched the extracted contours");
    return _Result;
}
}
