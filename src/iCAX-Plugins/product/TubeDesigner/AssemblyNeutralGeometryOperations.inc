// Execute function-owned neutral cutters through the common geometry engine.
// This adapter knows neither products nor machining rules.
TopoDS_Shape ApplyAssemblyNeutralOperations(const TopoDS_Shape& Base_,
    const ObjectMap& Request_, const gp_Trsf& DocumentToStock_)
{
    const auto _Found = Request_.find("neutralOperations");
    if (_Found == Request_.end()) return Base_;
    if (!_Found->second.Is<VariantArray>())
        throw std::invalid_argument("中立加工操作必须为列表");
    const auto _Operations = _Found->second.To<VariantArray>();
    if (_Operations.size() > 1024) throw std::invalid_argument("中立加工操作数量超限");
    if (_Operations.empty()) return Base_;
    std::ostringstream _BRep;
    BRepTools::Write(Base_, _BRep);
    VariantArray _Geometry{ObjectMap{{"key",std::string("material.raw")},
        {"operator",std::string("resource")}, {"arguments",ObjectMap{{"brep",_BRep.str()}}}}};
    std::string _Current = "material.raw";
    std::size_t _Index = 0;
    for (const auto& _Value : _Operations) {
        const auto _Entry = _Value.To<ObjectMap>();
        const auto _Nodes = _Entry.at("geometry").To<VariantArray>();
        if (_Nodes.empty() || _Nodes.size() > 8192) throw std::invalid_argument("中立刀具声明无效");
        const auto _Operation = GetRequiredObject(_Entry,"operation");
        if (GetString(_Operation,"kind") != "neutral-csg" || GetString(_Operation,"role") != "stock")
            throw std::invalid_argument("中立加工必须明确作用于原材");
        const auto _Prefix = "function." + std::to_string(_Index++) + ".";
        std::map<std::string,std::string> _Names;
        for (const auto& _Node : _Nodes) {
            const auto _Key = GetRequiredText(_Node.To<ObjectMap>(),"key",200);
            if (!_Names.emplace(_Key,_Prefix+std::to_string(_Names.size())).second)
                throw std::invalid_argument("中立刀具声明标识重复");
        }
        const std::function<Variant(const Variant&)> _Rewrite = [&](const Variant& _Input) -> Variant {
            if (_Input.Is<std::string>()) {
                const auto _Text = _Input.To<std::string>();
                const auto _It = _Names.find(_Text);
                return _It == _Names.end() ? _Input : Variant(_It->second);
            }
            if (_Input.Is<VariantArray>()) {
                auto _Result = _Input.To<VariantArray>();
                for (auto& _Child : _Result) _Child = _Rewrite(_Child);
                return _Result;
            }
            if (_Input.Is<ObjectMap>()) {
                auto _Result = _Input.To<ObjectMap>();
                for (auto& [_Key,_Child] : _Result) _Child = _Rewrite(_Child);
                return _Result;
            }
            return _Input;
        };
        for (const auto& _Node : _Nodes) _Geometry.push_back(_Rewrite(_Node));
        VariantArray _Tools;
        if (_Operation.contains("tools")) _Tools = _Operation.at("tools").To<VariantArray>();
        else _Tools.emplace_back(GetRequiredText(_Operation,"tool",200));
        if (_Tools.empty()) throw std::invalid_argument("中立加工没有刀具");
        const auto _Space = GetString(_Entry,"geometrySpace","document");
        if (_Space != "stock" && _Space != "document") throw std::invalid_argument("中立刀具坐标无效");
        gp_Trsf _Transform;
        if (_Operation.contains("matrix")) _Transform = AssemblyPairRigidMatrix(_Operation,"matrix");
        if (_Space == "document") _Transform = DocumentToStock_ * _Transform;
        const ObjectMap _Placement{{"origin",VariantArray{_Transform.Value(1,4),_Transform.Value(2,4),_Transform.Value(3,4)}},
            {"xAxis",VariantArray{_Transform.Value(1,1),_Transform.Value(2,1),_Transform.Value(3,1)}},
            {"yAxis",VariantArray{_Transform.Value(1,2),_Transform.Value(2,2),_Transform.Value(3,2)}},
            {"zAxis",VariantArray{_Transform.Value(1,3),_Transform.Value(2,3),_Transform.Value(3,3)}}};
        VariantArray _Inputs{_Current};
        std::size_t _ToolIndex = 0;
        for (auto& _Tool : _Tools) {
            const auto _LocalKey = _Tool.To<std::string>();
            if (!_Names.contains(_LocalKey)) throw std::invalid_argument("中立加工引用未知刀具");
            const auto _Placed = _Prefix+"placed."+std::to_string(_ToolIndex++);
            _Geometry.emplace_back(ObjectMap{{"key",_Placed},{"operator",std::string("transform")},
                {"inputs",VariantArray{_Names.at(_LocalKey)}},{"arguments",ObjectMap{{"placement",_Placement}}}});
            _Tool = _Placed; _Inputs.push_back(_Tool);
        }
        auto _Arguments = _Operation.contains("arguments") ? GetRequiredObject(_Operation,"arguments") : ObjectMap{};
        if (_Space == "document" && _Arguments.contains("keepConnectedTo")) {
            const auto _Point = _Arguments.at("keepConnectedTo").To<VariantArray>();
            if (_Point.size()!=3) throw std::invalid_argument("材料见证点无效");
            gp_Pnt _Witness(_Point[0].To<double>(),_Point[1].To<double>(),_Point[2].To<double>());
            _Witness.Transform(DocumentToStock_);
            _Arguments["keepConnectedTo"] = VariantArray{_Witness.X(),_Witness.Y(),_Witness.Z()};
        }
        _Arguments["operation"] = GetRequiredText(_Operation,"operation",20);
        _Arguments["target"] = _Current; _Arguments["tools"] = _Tools;
        _Current = _Prefix+"result";
        _Geometry.emplace_back(ObjectMap{{"key",_Current},{"operator",std::string("boolean")},
            {"inputs",std::move(_Inputs)},{"arguments",std::move(_Arguments)}});
    }
    const ObjectMap _Document{{"schema",std::string("icax.neutral-model")},{"schemaVersion",1ull},
        {"template",ObjectMap{{"id",std::string("icax.assembly-neutral-execution")},
            {"version",std::string("1.0.0")},{"packageDigest",std::string("resolved-function-operations")}}},
        {"geometry",std::move(_Geometry)}};
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
    const auto _Shape = iCAX::OpenCascade::EvaluateNeutralModel(_Model,{_Current}).At(_Current);
    if (_Shape.IsNull() || !BRepCheck_Analyzer(_Shape).IsValid())
        throw std::runtime_error("中立加工没有生成有效实体");
    return _Shape;
}
