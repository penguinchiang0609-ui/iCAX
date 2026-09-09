#include "pch.h"
#include "ExtrusionRecognitionService.h"
#include "SectionGeometry.h"
#include "ExtrudeRecognizesService.h"

#include "TemplateRuntime/PythonTemplateHost.h"
#include "TemplateRuntime/StandardJsonCodec.h"

#include <filesystem>

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
        VariantArray _Samples;
        _Samples.reserve(Edge_.Samples.size());
        for (const auto& _Point : Edge_.Samples)
        {
            _Samples.push_back(PointValue(_Point));
        }
        return Variant(ObjectMap{
            { "sourceEdgeId", static_cast<unsigned long long>(Edge_.SourceEdgeId) },
            { "start", PointValue(Edge_.Start) },
            { "end", PointValue(Edge_.End) },
            { "reversed", Edge_.bReversed },
            { "samples", std::move(_Samples) }
        });
    }

    VariantArray WirePoints(IN const SSectionWire& Wire_)
    {
        VariantArray _Points;
        for (const auto& _Edge : Wire_.Edges)
        {
            if (_Edge.Samples.empty())
            {
                if (_Points.empty()) _Points.push_back(PointValue(_Edge.Start));
                _Points.push_back(PointValue(_Edge.End));
                continue;
            }
            const auto _Begin = _Points.empty() ? 0u : 1u;
            for (std::size_t _Index = _Begin; _Index < _Edge.Samples.size(); ++_Index)
            {
                _Points.push_back(PointValue(_Edge.Samples[_Index]));
            }
        }
        if (_Points.size() > 1 && _Points.front() == _Points.back())
        {
            _Points.pop_back();
        }
        return _Points;
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
                { "signedArea", _Wire.dSignedArea },
                { "points", WirePoints(_Wire) },
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

    bool TryReadTransform(IN const Variant& Value_, OUT Transform3& Transform_)
    {
        if (!Value_.Is<VariantArray>() || Value_.To<VariantArray>().size() != 4)
        {
            return false;
        }
        const auto& _Rows = Value_.To<VariantArray>();
        Transform3 _Candidate;
        for (std::size_t _Row = 0; _Row < 4; ++_Row)
        {
            if (!_Rows[_Row].Is<VariantArray>()
                || _Rows[_Row].To<VariantArray>().size() != 4)
            {
                return false;
            }
            const auto& _Columns = _Rows[_Row].To<VariantArray>();
            for (std::size_t _Column = 0; _Column < 4; ++_Column)
            {
                const auto _Value = Number(_Columns[_Column]);
                if (!_Value) return false;
                _Candidate.Matrix.Values[_Row][_Column] = *_Value;
            }
        }
        const auto& _LastRow = _Candidate.Matrix.Values[3];
        if (std::abs(_LastRow[0]) > 1.0e-8
            || std::abs(_LastRow[1]) > 1.0e-8
            || std::abs(_LastRow[2]) > 1.0e-8
            || std::abs(_LastRow[3] - 1.0) > 1.0e-8)
        {
            return false;
        }
        Transform_ = _Candidate;
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
            _Result.Bounds = { _Minimum, _Maximum, false };
            const auto _DX = _Maximum.X - _Minimum.X;
            const auto _DY = _Maximum.Y - _Minimum.Y;
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
        Match_.bMatched = _Matched->second.To<bool>();
        if (!Match_.bMatched) return true;

        if (const auto _Parameters = Response_.find("parameters");
            _Parameters != Response_.end())
        {
            if (!_Parameters->second.Is<ObjectMap>())
            {
                strError_ = "Python fitter parameters must be an object";
                return false;
            }
            Match_.Parameters = _Parameters->second.To<ObjectMap>();
        }
        const auto _TRSF = Response_.find("trsf");
        if (_TRSF == Response_.end()
            || !TryReadTransform(_TRSF->second, Match_.TRSF))
        {
            strError_ = "Python fitter matched result must contain a valid 4x4 trsf";
            return false;
        }
        Match_.Placement.Origin = {};
        Match_.Placement.XDirection = { 1.0, 0.0 };
        Match_.Placement.ZDirection = { 0.0, 1.0 };
        if (const auto _Diagnostics = Response_.find("diagnostics");
            _Diagnostics != Response_.end()
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
        const auto _Script = _Iterator->path() / "profile.py";
        if (!std::filesystem::is_regular_file(_Script, _Error))
        {
            _Error.clear();
            continue;
        }
        _Result.push_back({
            _Iterator->path().filename().string(),
            std::filesystem::weakly_canonical(_Script, _Error).string(),
            {} });
        _Error.clear();
    }
    std::sort(_Result.begin(), _Result.end(), [](const auto& Left_, const auto& Right_) {
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
    if (OrderedFitters_.empty() || FitterOptions_.dLinearTolerance <= 0.0)
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

    ObjectMap _Context{
        { "linearTolerance", FitterOptions_.dLinearTolerance },
        { "axis", std::string("X") },
        { "sectionSchema", std::string("icax.section-contours.v1") }
    };
    const auto _Contours = MakeContourArray(_Sections.Wires);
    for (const auto& _Fitter : OrderedFitters_)
    {
        if (_Fitter.TypeID.empty() || _Fitter.ScriptPath.empty())
        {
            _Result.Diagnostics.push_back("Skipped Python fitter with empty type id or script path");
            continue;
        }
        try
        {
            ObjectMap _Request{
                { "protocol", std::string("icax.template-runtime") },
                { "protocolVersion", static_cast<unsigned int>(1) },
                { "operation", std::string("fit") },
                { "fitterPath", _Fitter.ScriptPath },
                { "packageDigest", _Fitter.PackageDigest },
                { "contours", _Contours },
                { "context", _Context }
            };
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
            if (!_Match.bMatched)
            {
                _Result.Diagnostics.push_back(_Fitter.TypeID + ": no match");
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
    _Result.Status = ERecognitionStatus::SectionTypeNotMatched;
    _Result.Diagnostics.push_back("No Python section fitter matched the extracted contours");
    return _Result;
}
}
