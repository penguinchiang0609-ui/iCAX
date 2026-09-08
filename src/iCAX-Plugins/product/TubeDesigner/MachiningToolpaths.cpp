#include "pch.h"
#include "MachiningToolpaths.h"
#include "Data/uuid.h"
#include <cmath>
#include <set>
#include <stdexcept>

namespace iCAX::TubeDesigner
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    namespace
    {
        double Number(const Variant& Value_)
        {
            if (Value_.Is<double>()) return Value_.To<double>();
            if (Value_.Is<float>()) return Value_.To<float>();
            if (Value_.Is<int>()) return Value_.To<int>();
            if (Value_.Is<unsigned long long>()) return static_cast<double>(Value_.To<unsigned long long>());
            if (Value_.Is<long long>()) return static_cast<double>(Value_.To<long long>());
            throw std::invalid_argument("刀路坐标必须为数字");
        }
        VariantArray Samples(const Variant& Value_, bool Direction_ = false)
        {
            if (!Value_.Is<VariantArray>()) throw std::invalid_argument("刀路坐标格式错误");
            VariantArray _Result;
            for (const auto& _Value : Value_.To<VariantArray>())
            {
                if (!_Value.Is<VariantArray>() || _Value.To<VariantArray>().size() != 3)
                    throw std::invalid_argument("刀路点必须有 X、Y、Z 三个坐标");
                VariantArray _Point;
                double _Norm = 0;
                for (const auto& _Coordinate : _Value.To<VariantArray>())
                {
                    const auto _Number = Number(_Coordinate);
                    if (!std::isfinite(_Number) || std::abs(_Number) > 1.0e8)
                        throw std::invalid_argument("刀路坐标超出范围");
                    _Point.emplace_back(_Number); _Norm += _Number * _Number;
                }
                if (Direction_)
                {
                    if (_Norm < 1.0e-12) throw std::invalid_argument("刀路方向不能为零向量");
                    for (auto& _Coordinate : _Point) _Coordinate = Number(_Coordinate) / std::sqrt(_Norm);
                }
                _Result.emplace_back(std::move(_Point));
            }
            return _Result;
        }
    }

    VariantArray ValidateMachiningToolpaths(const VariantArray& Paths_)
    {
        if (Paths_.size() > 20000) throw std::invalid_argument("单根母材刀路不能超过 20000 条");
        VariantArray _Paths;
        std::set<std::string> _IDs;
        std::size_t _PointCount = 0;
        for (const auto& _Value : Paths_)
        {
            if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("刀路数据格式错误");
            const auto _Path = _Value.To<ObjectMap>();
            const auto _ID = _Path.at("id").To<std::string>();
            if (_ID.empty() || _ID.size() > 160 || !_IDs.insert(_ID).second)
                throw std::invalid_argument("刀路编号为空或重复");
            const auto _Name = _Path.at("name").To<std::string>();
            if (_Name.empty() || _Name.size() > 400) throw std::invalid_argument("刀路名称无效");
            auto _Points = Samples(_Path.at("points"));
            _PointCount += _Points.size();
            if (_Points.size() < 2 || _PointCount > 500000) throw std::invalid_argument("刀路点数无效或超过 500000 点限制");
            const bool _Closed = _Path.at("closed").To<bool>();
            if (_Closed && _Points.size() < 3) throw std::invalid_argument("闭合刀路至少需要三个点");
            ObjectMap _Clean{ { "id", _ID }, { "name", _Name }, { "points", std::move(_Points) }, { "closed", _Closed },
                { "representation", std::string("sampled-polyline") } };
            for (const auto* _Key : { "directions", "normals" })
            {
                if (!_Path.contains(_Key)) continue;
                auto _Directions = Samples(_Path.at(_Key), true);
                if (_Directions.size() != _Clean.at("points").To<VariantArray>().size())
                    throw std::invalid_argument("刀姿数量与刀路点数不一致");
                _Clean[_Key] = std::move(_Directions);
            }
            for (const auto* _Key : { "origin", "sourcePart", "sourceFeature" })
                if (_Path.contains(_Key) && _Path.at(_Key).Is<std::string>())
                    _Clean[_Key] = _Path.at(_Key).To<std::string>().substr(0, 400);
            _Paths.emplace_back(std::move(_Clean));
        }
        return _Paths;
    }

    ObjectMap SerializeMachiningToolpaths(const iCAX::CAM::CamPathAnalysis& Analysis_)
    {
        VariantArray _Paths, _Diagnostics;
        for (const auto& _Item : Analysis_.Diagnostics)
            _Diagnostics.emplace_back(ObjectMap{ { "part", _Item.Part }, { "code", _Item.Code }, { "message", _Item.Message },
                { "severity", _Item.Error ? std::string("error") : std::string("warning") } });
        for (const auto& _Path : Analysis_.Paths)
        {
            if (_Path.Curve.Segments.size() != 1 || !std::holds_alternative<iCAX::GeometryData::Polyline3>(_Path.Curve.Segments[0].Curve))
                throw std::invalid_argument("Unsupported CamPath editor representation");
            VariantArray _Points, _Directions, _Normals;
            for (const auto& _Point : std::get<iCAX::GeometryData::Polyline3>(_Path.Curve.Segments[0].Curve).Points)
                _Points.emplace_back(VariantArray{ _Point.X, _Point.Y, _Point.Z });
            for (const auto& _Pose : _Path.Poses)
            {
                _Directions.emplace_back(VariantArray{ _Pose.ToolDirection.X, _Pose.ToolDirection.Y, _Pose.ToolDirection.Z });
                _Normals.emplace_back(VariantArray{ _Pose.SurfaceNormal.X, _Pose.SurfaceNormal.Y, _Pose.SurfaceNormal.Z });
            }
            ObjectMap _Serialized{ { "id", _Path.ID }, { "name", _Path.Name },
                { "closed", _Path.Curve.Closed }, { "points", std::move(_Points) },
                { "origin", std::string("analysis") }, { "sourcePart", _Path.SourcePart }, { "sourceFeature", _Path.SourceFeature } };
            if (!_Path.Poses.empty())
            {
                _Serialized["directions"] = std::move(_Directions);
                _Serialized["normals"] = std::move(_Normals);
            }
            _Paths.emplace_back(std::move(_Serialized));
        }
        if (_Paths.empty()) throw std::invalid_argument("未识别出可用刀路；请检查模型是否为有效的拉伸管材或型材。原刀路未改动。");
        return { { "paths", ValidateMachiningToolpaths(_Paths) }, { "diagnostics", std::move(_Diagnostics) },
            { "schemaVersion", 1 }, { "unit", std::string("mm") }, { "independent", true }, { "complete", Analysis_.Complete },
            { "representation", std::string("sampled-polyline") },
            { "analysisId", iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()) } };
    }
}
