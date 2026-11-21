// Shared neutral connection requirement. The caller supplies actual FINISHED
// parts in one product frame, after every machining/edit operation and before
// list merging or persistent publication. No product-family rules belong here.
struct SManufacturingNonInterference final
{
    std::string ConnectionKey, First, Second;
    double MaximumVolume = 0.0;
};

std::vector<SManufacturingNonInterference> ReadManufacturingNonInterference(
    const ObjectMap& Definition_)
{
    std::vector<SManufacturingNonInterference> _Result;
    const auto _Connections = Definition_.find("connections");
    if (_Connections == Definition_.end()) return _Result;
    std::map<std::pair<std::string, std::string>, double> _Seen;
    for (const auto& _Value : _Connections->second.To<VariantArray>())
    {
        const auto _Connection = _Value.To<ObjectMap>();
        const auto _Properties = _Connection.find("properties");
        if (_Properties == _Connection.end()) continue;
        const auto _Values = _Properties->second.To<ObjectMap>();
        const auto _Requirements = _Values.find("nonInterferencePairs");
        if (_Requirements == _Values.end()) continue;
        for (const auto& _Entry : _Requirements->second.To<VariantArray>())
        {
            const auto _Requirement = _Entry.To<ObjectMap>();
            const auto _Items = _Requirement.at("items").To<VariantArray>();
            if (_Items.size() != 2)
                throw std::invalid_argument("装配无干涉要求必须对应两根实际构件");
            auto _First = _Items[0].To<std::string>(), _Second = _Items[1].To<std::string>();
            if (_First.empty() || _Second.empty() || _First == _Second)
                throw std::invalid_argument("装配无干涉要求的构件标识无效");
            if (_Second < _First) std::swap(_First, _Second);
            const double _Tolerance = _Requirement.at("maximumIntersectionVolumeMm3").To<double>();
            if (!std::isfinite(_Tolerance) || _Tolerance < 0)
                throw std::invalid_argument("装配无干涉要求的实体穿透容差无效");
            const auto _Pair = std::make_pair(_First, _Second);
            if (const auto _Found = _Seen.find(_Pair); _Found != _Seen.end())
            {
                if (_Found->second != _Tolerance)
                    throw std::invalid_argument("同一装配构件对的实体穿透容差不一致");
                continue;
            }
            _Seen.emplace(_Pair, _Tolerance);
            _Result.push_back({_Connection.at("key").To<std::string>(), _First, _Second, _Tolerance});
        }
    }
    return _Result;
}

VariantArray ValidateManufacturingNonInterference(
    const std::vector<SManufacturingNonInterference>& Requirements_,
    const std::map<std::string, TopoDS_Shape>& FinishedInProduct_)
{
    if (Requirements_.empty()) return {};
    return iCAX::Tasks::Run([&]() {
        VariantArray _Checks;
        for (const auto& _Required : Requirements_)
        {
            if (!FinishedInProduct_.contains(_Required.First) || !FinishedInProduct_.contains(_Required.Second))
                throw std::invalid_argument("装配无干涉要求引用的最终构件不存在");
            const auto& _A = FinishedInProduct_.at(_Required.First);
            const auto& _B = FinishedInProduct_.at(_Required.Second);
            if (_A.IsNull() || _B.IsNull()
                || !BRepCheck_Analyzer(_A).IsValid() || !BRepCheck_Analyzer(_B).IsValid())
                throw std::runtime_error("装配无干涉校验缺少有效的最终加工实体");
            Bnd_Box _BoundsA, _BoundsB;
            BRepBndLib::AddOptimal(_A, _BoundsA, false, false);
            BRepBndLib::AddOptimal(_B, _BoundsB, false, false);
            if (_BoundsA.IsVoid() || _BoundsB.IsVoid())
                throw std::runtime_error("装配无干涉校验无法确定最终实体范围");
            double _Volume = 0.0;
            if (!_BoundsA.IsOut(_BoundsB))
            {
                BRepAlgoAPI_Common _Common;
                NCollection_List<TopoDS_Shape> _Arguments, _Tools;
                _Arguments.Append(_A); _Tools.Append(_B);
                _Common.SetArguments(_Arguments); _Common.SetTools(_Tools);
                _Common.SetNonDestructive(true); _Common.Build();
                if (!_Common.IsDone())
                    throw std::runtime_error("无法精确核对最终加工构件的实体穿透");
                if (!_Common.Shape().IsNull())
                {
                    if (!BRepCheck_Analyzer(_Common.Shape()).IsValid())
                        throw std::runtime_error("最终加工构件的实体穿透运算无效");
                    GProp_GProps _Properties;
                    BRepGProp::VolumeProperties(_Common.Shape(), _Properties);
                    _Volume = std::abs(_Properties.Mass());
                }
            }
            if (!std::isfinite(_Volume) || _Volume > _Required.MaximumVolume)
                throw std::runtime_error("构件 " + _Required.First + " 与 "
                    + _Required.Second + " 实际加工后相撞，穿透体积 "
                    + std::to_string(_Volume) + " mm³；请修改连接工艺或公头尺寸");
            _Checks.emplace_back(ObjectMap{{"connectionKey", _Required.ConnectionKey},
                {"items", VariantArray{_Required.First, _Required.Second}},
                {"status", std::string("pass")}, {"intersectionVolumeMm3", _Volume},
                {"maximumIntersectionVolumeMm3", _Required.MaximumVolume},
                {"scope", std::string("actual-finished-solids-in-product-frame")}});
        }
        return _Checks;
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
}
