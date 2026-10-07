// Execute function-owned neutral cutters through the common geometry engine.
// This adapter knows neither products nor machining rules.
std::size_t AssemblyNativeGeometryConcurrency()
{
    char _Workers[8]{};
    if(GetEnvironmentVariableA("ICAX_ASSEMBLY_GEOMETRY_WORKERS",_Workers,sizeof(_Workers))==0) {
#ifdef _DEBUG
        // Debug CRT/iterator bookkeeping makes the allocation-heavy fixture
        // faster serially. Release uses bounded private-shape CAD workers.
        return 1;
#else
        const auto _Hardware = std::thread::hardware_concurrency();
        return std::min(4u, std::max(1u, _Hardware > 1 ? _Hardware - 1 : 1u));
#endif
    }
    // Retain the override for reproducible native concurrency comparisons.
    const std::string _Value(_Workers);
    if(_Value=="1")return 1;
    if(_Value=="2")return 2;
    if(_Value=="4")return 4;
    if(_Value=="8")return 8;
    throw std::invalid_argument("ICAX_ASSEMBLY_GEOMETRY_WORKERS must be 1, 2, 4 or 8");
}

struct SAssemblyNeutralOperationInput final
{
    TopoDS_Shape Base;
    ObjectMap Request;
    gp_Trsf DocumentToStock;
};

std::vector<TopoDS_Shape> ApplyAssemblyNeutralOperationsBatch(
    const std::vector<SAssemblyNeutralOperationInput>& Inputs_, ObjectMap* Performance_ = nullptr,
    bool OptimizeUnobservedOperations_ = false,
    const std::function<void(std::size_t, const TopoDS_Shape&)>& OnCompleted_ = {})
{
    const auto _Started = std::chrono::steady_clock::now();
    VariantArray _Geometry;
    std::vector<TopoDS_Shape> _Results(Inputs_.size());
    std::vector<std::string> _Roots(Inputs_.size());
    std::vector<std::pair<TopoDS_Shape, std::string>> _Bases;
    std::map<std::string, std::vector<std::string>> _ToolGraphs;
    std::map<std::string, std::string> _Nodes;
    std::size_t _GraphReuse = 0, _NodeReuse = 0;
    const auto _AddNode = [&](std::string Operator_, VariantArray Dependencies_, ObjectMap Arguments_) {
        ObjectMap _Node;
        _Node.emplace("operator", std::move(Operator_));
        _Node.emplace("inputs", std::move(Dependencies_));
        _Node.emplace("arguments", std::move(Arguments_));
        const auto _Identity = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_Node);
        if (const auto _Existing = _Nodes.find(_Identity); _Existing != _Nodes.end()) {
            ++_NodeReuse;
            return _Existing->second;
        }
        const auto _Key = "assembly.node." + std::to_string(_Geometry.size());
        _Node["key"] = _Key;
        _Geometry.emplace_back(std::move(_Node));
        _Nodes.emplace(_Identity, _Key);
        return _Key;
    };
    const auto _RewriteWith = [](const Variant& Input_, const std::map<std::string, std::string>& Names_) {
        const std::function<Variant(const Variant&)> _Rewrite = [&](const Variant& _Input) -> Variant {
            if (_Input.Is<std::string>()) {
                const auto _Text = _Input.To<std::string>();
                const auto _It = Names_.find(_Text);
                return _It == Names_.end() ? _Input : Variant(_It->second);
            }
            if (_Input.Is<VariantArray>()) {
                const auto& _Items = std::get<VariantArray>(_Input.m_Value);
                VariantArray _Result;
                _Result.reserve(_Items.size());
                for (const auto& _Child : _Items) _Result.emplace_back(_Rewrite(_Child));
                return _Result;
            }
            if (_Input.Is<ObjectMap>()) {
                ObjectMap _Result;
                for (const auto& [_Key, _Child] : std::get<ObjectMap>(_Input.m_Value))
                    _Result.emplace(_Key, _Rewrite(_Child));
                return _Result;
            }
            return _Input;
        };
        return _Rewrite(Input_);
    };
    for (std::size_t _StockIndex = 0; _StockIndex < Inputs_.size(); ++_StockIndex) {
        const auto& _Input = Inputs_[_StockIndex];
        const auto _Found = _Input.Request.find("neutralOperations");
        if (_Found == _Input.Request.end()) { _Results[_StockIndex] = _Input.Base; continue; }
        if (!_Found->second.Is<VariantArray>()) throw std::invalid_argument("中立加工操作必须为列表");
        const auto& _Operations = std::get<VariantArray>(_Found->second.m_Value);
        if (_Operations.size() > 1024) throw std::invalid_argument("中立加工操作数量超限");
        if (_Operations.empty()) { _Results[_StockIndex] = _Input.Base; continue; }
        const auto _ExistingBase = std::find_if(_Bases.begin(), _Bases.end(), [&](const auto& Base_) {
            return Base_.first.IsEqual(_Input.Base);
        });
        std::string _Current;
        if (_ExistingBase != _Bases.end()) _Current = _ExistingBase->second;
        else {
            std::ostringstream _BRep; BRepTools::Write(_Input.Base, _BRep);
            _Current = _AddNode("resource", {}, ObjectMap{{"brep", _BRep.str()}});
            _Bases.emplace_back(_Input.Base, _Current);
        }
        for (const auto& _Value : _Operations) {
        const auto& _Entry = std::get<ObjectMap>(_Value.m_Value);
        const auto& _Nodes = std::get<VariantArray>(_Entry.at("geometry").m_Value);
        if (_Nodes.empty() || _Nodes.size() > 8192) throw std::invalid_argument("中立刀具声明无效");
        const auto& _Operation = GetRequiredObjectView(_Entry,"operation");
        if (GetString(_Operation,"kind") != "neutral-csg" || GetString(_Operation,"role") != "stock")
            throw std::invalid_argument("中立加工必须明确作用于原材");
        std::map<std::string,std::string> _LocalNames;
        for (const auto& _Node : _Nodes) {
            const auto _Key = GetRequiredText(std::get<ObjectMap>(_Node.m_Value),"key",200);
            if (!_LocalNames.emplace(_Key,"local."+std::to_string(_LocalNames.size())).second)
                throw std::invalid_argument("中立刀具声明标识重复");
        }
        // Compare complete exact graphs after renaming only graph identifiers.
        // The node order, operators, all coordinates/arguments and references
        // remain part of equivalence; no product rule or size approximation.
        const auto _GraphIdentity = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
            _RewriteWith(_Entry.at("geometry"), _LocalNames));
        std::map<std::string,std::string> _Names;
        if (const auto _Shared = _ToolGraphs.find(_GraphIdentity); _Shared != _ToolGraphs.end()) {
            std::size_t _Index = 0;
            for (const auto& _Node : _Nodes)
                _Names.emplace(GetRequiredText(std::get<ObjectMap>(_Node.m_Value),"key",200), _Shared->second.at(_Index++));
            ++_GraphReuse;
        } else {
            std::vector<std::string> _GlobalNames;
            const auto _Prefix = "assembly.graph." + std::to_string(_ToolGraphs.size()) + ".";
            for (const auto& _Node : _Nodes) {
                const auto _Key = _Prefix + std::to_string(_GlobalNames.size());
                _Names.emplace(GetRequiredText(std::get<ObjectMap>(_Node.m_Value),"key",200), _Key);
                _GlobalNames.push_back(_Key);
            }
            for (const auto& _Node : _Nodes) _Geometry.push_back(_RewriteWith(_Node, _Names));
            _ToolGraphs.emplace(_GraphIdentity, std::move(_GlobalNames));
        }
        VariantArray _Tools;
        if (_Operation.contains("tools")) _Tools = _Operation.at("tools").To<VariantArray>();
        else _Tools.emplace_back(GetRequiredText(_Operation,"tool",200));
        if (_Tools.empty()) throw std::invalid_argument("中立加工没有刀具");
        const auto _Space = GetString(_Entry,"geometrySpace","document");
        if (_Space != "stock" && _Space != "document") throw std::invalid_argument("中立刀具坐标无效");
        gp_Trsf _Transform;
        if (_Operation.contains("matrix")) _Transform = AssemblyPairRigidMatrix(_Operation,"matrix");
        if (_Space == "document") _Transform = _Input.DocumentToStock * _Transform;
        const ObjectMap _Placement{{"origin",VariantArray{_Transform.Value(1,4),_Transform.Value(2,4),_Transform.Value(3,4)}},
            {"xAxis",VariantArray{_Transform.Value(1,1),_Transform.Value(2,1),_Transform.Value(3,1)}},
            {"yAxis",VariantArray{_Transform.Value(1,2),_Transform.Value(2,2),_Transform.Value(3,2)}},
            {"zAxis",VariantArray{_Transform.Value(1,3),_Transform.Value(2,3),_Transform.Value(3,3)}}};
        VariantArray _Inputs{_Current};
        for (auto& _Tool : _Tools) {
            const auto _LocalKey = _Tool.To<std::string>();
            if (!_Names.contains(_LocalKey)) throw std::invalid_argument("中立加工引用未知刀具");
            const auto _Placed = _AddNode("transform", VariantArray{_Names.at(_LocalKey)}, ObjectMap{{"placement",_Placement}});
            _Tool = _Placed; _Inputs.push_back(_Tool);
        }
        auto _Arguments = _Operation.contains("arguments") ? GetRequiredObject(_Operation,"arguments") : ObjectMap{};
        if (_Space == "document" && _Arguments.contains("keepConnectedTo")) {
            const auto _Point = _Arguments.at("keepConnectedTo").To<VariantArray>();
            if (_Point.size()!=3) throw std::invalid_argument("材料见证点无效");
            gp_Pnt _Witness(_Point[0].To<double>(),_Point[1].To<double>(),_Point[2].To<double>());
            _Witness.Transform(_Input.DocumentToStock);
            _Arguments["keepConnectedTo"] = VariantArray{_Witness.X(),_Witness.Y(),_Witness.Z()};
        }
        _Arguments["operation"] = GetRequiredText(_Operation,"operation",20);
        _Arguments["target"] = _Current; _Arguments["tools"] = _Tools;
        _Current = _AddNode("boolean", std::move(_Inputs), std::move(_Arguments));
        }
        _Roots[_StockIndex] = _Current;
    }
    std::vector<std::string> _Selected;
    for (const auto& _Root : _Roots) if (!_Root.empty()) _Selected.push_back(_Root);
    // Multiple stocks may select the same authored root. Observe that root once
    // in the evaluator, then deliver every stock index on its caller thread.
    std::map<std::string, std::vector<std::size_t>> _IndicesByRoot;
    std::set<std::string> _Validated;
    double _ValidationMs = 0;
    for (std::size_t _Index = 0; _Index < _Roots.size(); ++_Index) {
        if (_Roots[_Index].empty()) {
            if (OnCompleted_) OnCompleted_(_Index, _Results[_Index]);
        } else _IndicesByRoot[_Roots[_Index]].push_back(_Index);
    }
    if (_Selected.empty()) return _Results;
    const auto _Prepared = std::chrono::steady_clock::now();
    ObjectMap _Document{{"schema",std::string("icax.neutral-model")},{"schemaVersion",1ull},
        {"template",ObjectMap{{"id",std::string("icax.assembly-neutral-execution")},
            {"version",std::string("1.0.0")},{"packageDigest",std::string("resolved-function-operations")}}}};
    _Document.emplace("geometry",std::move(_Geometry));
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_Document));
    const auto _Parsed = std::chrono::steady_clock::now();
    if(GetEnvironmentVariableA("ICAX_PROFILE_DISASSEMBLY",nullptr,0)>0) {
        const auto _Directory=std::filesystem::current_path()/"output/tests/disassembly-performance";
        std::filesystem::create_directories(_Directory);
        std::ofstream(_Directory/"neutral-batch.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_Document);
        VariantArray _RootsDocument,_BaseDocument;
        for(const auto& _Root:_Selected)_RootsDocument.emplace_back(_Root);
        for(const auto& _Base:_Bases)_BaseDocument.emplace_back(_Base.second);
        std::ofstream(_Directory/"neutral-batch-roots.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
            ObjectMap{{"roots",std::move(_RootsDocument)},{"baseRoots",std::move(_BaseDocument)}});
    }
    const auto _Recorded=std::chrono::steady_clock::now();
    iCAX::OpenCascade::SNeutralModelEvaluationOptions _Options;
    _Options.MaximumConcurrency=AssemblyNativeGeometryConcurrency();
    _Options.CombineUnobservedSubtractions = true;
    _Options.ReuseRigidPrototypes = true;
    _Options.IntersectMatchingExtrusions = OptimizeUnobservedOperations_;
    _Options.ExpandSubtractionToolUnions = OptimizeUnobservedOperations_;
    _Options.SuppressUnusedBooleanHistory = OptimizeUnobservedOperations_;
    if (OnCompleted_) _Options.OnSelectedRootReady = [&](const std::string& Key_, const TopoDS_Shape& Shape_) {
        const auto _ValidationStarted = std::chrono::steady_clock::now();
        if (_Validated.emplace(Key_).second && (Shape_.IsNull() || !BRepCheck_Analyzer(Shape_).IsValid()))
            throw std::runtime_error("中立加工没有生成有效实体");
        _ValidationMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - _ValidationStarted).count();
        for (const auto _Index : _IndicesByRoot.at(Key_)) {
            _Results[_Index] = Shape_;
            OnCompleted_(_Index, _Results[_Index]);
        }
    };
    const auto _Evaluated = iCAX::OpenCascade::EvaluateNeutralModel(_Model,_Selected,_Options);
    const auto _Executed = std::chrono::steady_clock::now();
    const auto _FinalValidationStarted = std::chrono::steady_clock::now();
    for (std::size_t _Index = 0; _Index < _Roots.size(); ++_Index) if (!_Roots[_Index].empty()) {
        _Results[_Index] = _Evaluated.At(_Roots[_Index]);
        if (_Validated.emplace(_Roots[_Index]).second
            && (_Results[_Index].IsNull() || !BRepCheck_Analyzer(_Results[_Index]).IsValid()))
            throw std::runtime_error("中立加工没有生成有效实体");
    }
    _ValidationMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - _FinalValidationStarted).count();
    if (Performance_) {
        (*Performance_)["neutralGraph"] = std::chrono::duration<double,std::milli>(_Prepared-_Started).count();
        (*Performance_)["neutralParse"] = std::chrono::duration<double,std::milli>(_Parsed-_Prepared).count();
        (*Performance_)["neutralFixture"] = std::chrono::duration<double,std::milli>(_Recorded-_Parsed).count();
        (*Performance_)["neutralEvaluate"] = std::chrono::duration<double,std::milli>(_Executed-_Recorded).count();
        (*Performance_)["neutralValidation"] = _ValidationMs;
        (*Performance_)["neutralGraphReused"] = static_cast<unsigned long long>(_GraphReuse);
        (*Performance_)["neutralNodeReused"] = static_cast<unsigned long long>(_NodeReuse);
        (*Performance_)["neutralNodes"] = static_cast<unsigned long long>(_Model.Geometry.size());
        (*Performance_)["neutralBasePrototypes"] = static_cast<unsigned long long>(_Bases.size());
    }
    return _Results;
}

TopoDS_Shape ApplyAssemblyNeutralOperations(const TopoDS_Shape& Base_,
    const ObjectMap& Request_, const gp_Trsf& DocumentToStock_)
{
    return ApplyAssemblyNeutralOperationsBatch({{Base_,Request_,DocumentToStock_}}).front();
}
