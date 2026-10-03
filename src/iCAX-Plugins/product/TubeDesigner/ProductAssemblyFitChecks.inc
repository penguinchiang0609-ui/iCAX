// Verify a committed product node against the product members' declared
// centerlines. This is a topology check, not a cut-surface or fit certificate.
struct SAssemblyNodeGeometryUnsupported final : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

ObjectMap AssemblyVerifyCommittedNodeGeometry(
    const iCAX::TemplateRuntime::SNeutralModel& Model_,
    const iCAX::TemplateRuntime::SModelRelationship& Relationship_)
{
    ObjectMap _Result{{"connectionKey",Relationship_.Key},
        {"nodeGeometryStatus",std::string("unsupported")},
        {"detail",std::string("该成品连接没有可核对的中心线锚点")}};
    if (Relationship_.Kind != "assembly") return _Result;
    const auto _TopologyIt = Relationship_.Properties.find("topology");
    if (_TopologyIt == Relationship_.Properties.end()) return _Result;
    if (!_TopologyIt->second.Is<std::string>())
    {
        _Result["nodeGeometryStatus"] = std::string("invalid");
        _Result["detail"] = std::string("成品拓扑声明无效");
        return _Result;
    }
    const auto _Topology = _TopologyIt->second.To<std::string>();
    _Result["topology"] = _Topology;
    const bool _OrthogonalCorner = _Topology == "orthogonal-corner";
    if (_Topology != "L" && _Topology != "T" && _Topology != "X"
        && !_OrthogonalCorner) return _Result;
    constexpr double _ToleranceMm = 0.05;
    try
    {
        const auto _Required = [](const ObjectMap& Values_, const char* Key_) -> const Variant& {
            const auto _It = Values_.find(Key_);
            if (_It == Values_.end())
                throw SAssemblyNodeGeometryUnsupported(std::string("缺少 ") + Key_ + " 声明");
            return _It->second;
        };
        const auto _Text = [&_Required](const ObjectMap& Values_, const char* Key_) {
            const auto& _Value = _Required(Values_,Key_);
            if (!_Value.Is<std::string>() || _Value.To<std::string>().empty())
                throw std::invalid_argument(std::string(Key_) + " 声明无效");
            return _Value.To<std::string>();
        };
        const auto _Point = [&_Required](const ObjectMap& Values_, const char* Key_) {
            const auto& _Value = _Required(Values_,Key_);
            if (!_Value.Is<VariantArray>())
                throw std::invalid_argument(std::string(Key_) + " 须为三维坐标");
            const auto _Coords = _Value.To<VariantArray>();
            if (_Coords.size() != 3)
                throw std::invalid_argument(std::string(Key_) + " 须有三个坐标");
            const double _X = ToDouble(_Coords[0],Key_);
            const double _Y = ToDouble(_Coords[1],Key_);
            const double _Z = ToDouble(_Coords[2],Key_);
            if (!std::isfinite(_X) || !std::isfinite(_Y) || !std::isfinite(_Z))
                throw std::invalid_argument(std::string(Key_) + " 须为有限坐标");
            return gp_Pnt(_X,_Y,_Z);
        };
        const auto _Number = [&_Required](const ObjectMap& Values_, const char* Key_) {
            const double _Value = ToDouble(_Required(Values_,Key_),Key_);
            if (!std::isfinite(_Value))
                throw std::invalid_argument(std::string(Key_) + " 须为有限数值");
            return _Value;
        };
        const std::size_t _MemberCount = _OrthogonalCorner ? 3 : 2;
        if (Relationship_.ItemKeys.size() != _MemberCount)
            throw std::invalid_argument(_OrthogonalCorner
                ? "三向直角成品节点必须恰有三根构件" : "L/T/X 成品节点必须恰有两根构件");
        const gp_Pnt _Node = _Point(Relationship_.Properties,"centerlinePoint");
        const auto& _RowsValue = _Required(Relationship_.Properties,"participantAnchors");
        if (!_RowsValue.Is<VariantArray>())
            throw std::invalid_argument("节点构件锚点声明无效");
        const auto _Rows = _RowsValue.To<VariantArray>();
        if (_Rows.size() < _MemberCount)
            throw SAssemblyNodeGeometryUnsupported("成品节点缺少构件锚点声明");
        if (_Rows.size() > _MemberCount)
            throw std::invalid_argument("成品节点声明了多余的构件锚点");
        const std::set<std::string> _ExpectedKeys(
            Relationship_.ItemKeys.begin(),Relationship_.ItemKeys.end());
        if (_ExpectedKeys.size() != _MemberCount)
            throw std::invalid_argument("成品节点重复引用同一根构件");
        std::set<std::string> _CheckedKeys;
        std::vector<gp_Dir> _Axes;
        std::size_t _EndCount = 0, _SideCount = 0;
        double _MaxDeviation = 0.0;
        const auto _Match = [&](const gp_Pnt& Point_, const gp_Pnt& Expected_,
            const std::string& Label_) {
            const double _Distance = Point_.Distance(Expected_);
            _MaxDeviation = std::max(_MaxDeviation,_Distance);
            if (_Distance > _ToleranceMm)
                throw std::invalid_argument(Label_ + " 与产品节点中心线位置不一致");
        };
        for (const auto& _Value : _Rows)
        {
            if (!_Value.Is<ObjectMap>())
                throw std::invalid_argument("节点构件锚点须为对象");
            const auto _Row = _Value.To<ObjectMap>();
            const auto _Key = _Text(_Row,"itemKey");
            if (!_ExpectedKeys.contains(_Key) || !_CheckedKeys.insert(_Key).second)
                throw std::invalid_argument("节点锚点与真实构件引用不一致");
            const auto _AnchorIt = _Row.find("anchor");
            if (_AnchorIt != _Row.end() && !_AnchorIt->second.Is<ObjectMap>())
                throw std::invalid_argument("构件定位锚点无效");
            const auto _Anchor = _AnchorIt == _Row.end() ? _Row : _AnchorIt->second.To<ObjectMap>();
            if (_AnchorIt != _Row.end())
            {
                for (const char* _Field : {"kind","end","face","reference","approachFace"})
                    if (_Row.contains(_Field) && _Text(_Row,_Field) != _Text(_Anchor,_Field))
                        throw std::invalid_argument(std::string("构件锚点的扁平与嵌套 ")
                            + _Field + " 声明不一致");
                if (_Row.contains("station")
                    && std::abs(_Number(_Row,"station") - _Number(_Anchor,"station"))
                        > _ToleranceMm)
                    throw std::invalid_argument("构件锚点的扁平与嵌套站位不一致");
                if (_Row.contains("stockAllowance")
                    && std::abs(_Number(_Row,"stockAllowance")
                        - _Number(_Anchor,"stockAllowance")) > _ToleranceMm)
                    throw std::invalid_argument("构件锚点的扁平与嵌套原管余量不一致");
            }
            const auto _Kind = _Text(_Anchor,"kind");
            const auto& _Item = FindModelItem(Model_,_Key);
            if (!_Item.Properties.contains("assemblyFrame.member"))
                throw SAssemblyNodeGeometryUnsupported("构件缺少 assemblyFrame.member 中心线声明");
            const auto [_Start,_End] = AssemblyMemberFrame(_Item.Properties);
            const double _Length = _Start.Distance(_End);
            if (_Length <= 2.0 * _ToleranceMm)
                throw std::invalid_argument("构件中心线长度太短");
            const auto _Frame = GetRequiredObject(_Item.Properties,"assemblyFrame.member");
            if (const auto _DeclaredLength = _Frame.find("axisLength");
                _DeclaredLength != _Frame.end()
                && std::abs(_Number(_Frame,"axisLength") - _Length)
                    > _ToleranceMm)
                throw std::invalid_argument("构件中心线长度与已提交长度声明不一致");
            const auto _Stock = AssemblyMemberStockInterval(_Item.Properties,_Length);
            const gp_Dir _Axis(gp_Vec(_Start,_End));
            gp_Pnt _AnchorPoint;
            double _ExpectedStation = 0.0;
            if (_Kind == "end")
            {
                ++_EndCount;
                const auto _EndName = _Text(_Anchor,"end");
                if (_EndName == "start") { _AnchorPoint = _Start; _ExpectedStation = 0.0; }
                else if (_EndName == "end") { _AnchorPoint = _End; _ExpectedStation = _Length; }
                else throw std::invalid_argument("端部锚点只能是 start 或 end");
                const double _ExpectedAllowance = _EndName == "start"
                    ? -_Stock.StartStation : _Stock.EndStation - _Length;
                if (_Stock.Declared && !_Anchor.contains("stockAllowance"))
                    throw std::invalid_argument("端部锚点缺少物理原管预留声明");
                if (_Anchor.contains("stockAllowance")
                    && (std::abs(_Number(_Anchor,"stockAllowance")
                        - _ExpectedAllowance) > _ToleranceMm
                        || _Number(_Anchor,"stockAllowance") < -_ToleranceMm))
                    throw std::invalid_argument("端部锚点预留与物理原管区间不一致");
                gp_Dir _Away = _Axis;
                if (_EndName == "end") _Away.Reverse();
                _Axes.push_back(_Away);
            }
            else if (_Kind == "side")
            {
                ++_SideCount;
                if (_Anchor.contains("stockAllowance"))
                    throw std::invalid_argument("侧面锚点不能声明端部原管预留");
                if (_Text(_Anchor,"reference") != "start")
                    throw std::invalid_argument("侧壁站位必须从构件起端计量");
                (void)_Text(_Anchor,"face");
                _ExpectedStation = _Number(_Anchor,"station");
                if (_ExpectedStation <= _ToleranceMm
                    || _ExpectedStation >= _Length - _ToleranceMm)
                    throw std::invalid_argument("侧壁站位不在构件内部");
                _AnchorPoint = _Start.Translated(gp_Vec(_Axis) * _ExpectedStation);
                _Axes.push_back(_Axis);
            }
            else throw std::invalid_argument("节点声明了不支持的构件锚点类型");
            const double _DeclaredStation = _Number(_Row,"localAxialStation");
            if (std::abs(_DeclaredStation - _ExpectedStation) > _ToleranceMm)
                throw std::invalid_argument("构件锚点站位与中心线实际位置不一致");
            _Match(_AnchorPoint,_Node,_Key);
            _Match(_Point(_Row,"centerlinePoint"),_AnchorPoint,_Key + " 声明点");
        }
        if (_CheckedKeys != _ExpectedKeys)
            throw std::invalid_argument("节点构件锚点缺失");
        if ((_Topology == "L" && (_EndCount != 2 || _SideCount != 0))
            || (_Topology == "T" && (_EndCount != 1 || _SideCount != 1))
            || (_Topology == "X" && (_EndCount != 0 || _SideCount != 2))
            || (_OrthogonalCorner && (_EndCount != 3 || _SideCount != 0)))
            throw std::invalid_argument("构件端/侧位置与成品 L/T/X 拓扑不一致");
        if (_OrthogonalCorner)
        {
            if (_Axes.size() != 3)
                throw std::invalid_argument("三向直角节点缺少真实构件方向");
            VariantArray _Angles;
            for (std::size_t _First = 0; _First < _Axes.size(); ++_First)
                for (std::size_t _Second = _First + 1; _Second < _Axes.size(); ++_Second)
                {
                    const double _Angle = std::acos(std::clamp(
                        _Axes[_First].Dot(_Axes[_Second]),-1.0,1.0))
                        * 180.0 / std::numbers::pi;
                    if (std::abs(_Angle - 90.0) > 0.05)
                        throw std::invalid_argument("三向直角节点的三根中心线必须两两垂直");
                    _Angles.emplace_back(_Angle);
                }
            _Result["axisAnglesDeg"] = std::move(_Angles);
            _Result["nodeGeometryStatus"] = std::string("verified");
            _Result["detail"] = std::string("三根中心线和端点一致且两两垂直；尚未核对加工后实体配合");
            _Result["maxDeviationMm"] = _MaxDeviation;
            return _Result;
        }
        if (_Axes.size() != 2 || std::abs(_Axes[0].Dot(_Axes[1])) > 0.9999996)
            throw std::invalid_argument("两根构件中心线共线，不能构成 L/T/X 节点");
        const double _AxisDot=std::max(-1.0,std::min(1.0,_Axes[0].Dot(_Axes[1])));
        _Result["axisAngleDeg"] = std::acos(_AxisDot)
            * 180.0 / 3.14159265358979323846;
        _Result["nodeGeometryStatus"] = std::string("verified");
        _Result["detail"] = std::string("中心线与锚点一致；尚未核对加工后实体配合");
        _Result["maxDeviationMm"] = _MaxDeviation;
    }
    catch (const SAssemblyNodeGeometryUnsupported& _Error)
    {
        _Result["detail"] = std::string(_Error.what());
    }
    catch (const std::exception& _Error)
    {
        _Result["nodeGeometryStatus"] = std::string("invalid");
        _Result["detail"] = std::string(_Error.what());
    }
    return _Result;
}
