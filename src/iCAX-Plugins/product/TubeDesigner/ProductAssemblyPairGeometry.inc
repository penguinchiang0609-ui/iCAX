// Include after ProductAssemblyBindingGeometry.inc, inside TubeDesignerSDO.cpp's
// anonymous namespace. TubeDesignerSDO.cpp must include BRepAlgoAPI_Common.hxx,
// BRepExtrema_DistShapeShape.hxx, and BRepPrimAPI_MakeBox.hxx before the namespace.
// The caller supplies the two *finished* manufacturing BReps in part coordinates
// and the mappings returned by AssemblySourceToPartMapping. This measures only
// material near a node; it does not verify a tab, slot, specified gap, or fit.

double AssemblyPairMatrixNumber(const iCAX::Data::Variant& Value_)
{
    if (Value_.Is<double>()) return Value_.To<double>();
    if (Value_.Is<float>()) return static_cast<double>(Value_.To<float>());
    if (Value_.Is<unsigned long long>())
        return static_cast<double>(Value_.To<unsigned long long>());
    if (Value_.Is<long long>()) return static_cast<double>(Value_.To<long long>());
    if (Value_.Is<unsigned int>()) return static_cast<double>(Value_.To<unsigned int>());
    if (Value_.Is<int>()) return static_cast<double>(Value_.To<int>());
    throw std::invalid_argument("Assembly pair matrix entry is not numeric");
}

std::string AssemblyPairMappingString(const ObjectMap& Mapping_, const char* Key_)
{
    const auto _Found = Mapping_.find(Key_);
    return _Found != Mapping_.end() && _Found->second.Is<std::string>()
        ? _Found->second.To<std::string>() : std::string();
}

gp_Trsf AssemblyPairRigidMatrix(const ObjectMap& Mapping_, const char* Key_)
{
    const auto _Found = Mapping_.find(Key_);
    if (_Found == Mapping_.end() || !_Found->second.Is<VariantArray>())
        throw std::invalid_argument(std::string("Assembly pair mapping lacks ") + Key_);
    const auto _Matrix = _Found->second.To<VariantArray>();
    if (_Matrix.size() != 16)
        throw std::invalid_argument(std::string("Assembly pair mapping has invalid ") + Key_);

    std::array<double,16> _Values{};
    for (std::size_t _Index = 0; _Index < _Values.size(); ++_Index)
    {
        _Values[_Index] = AssemblyPairMatrixNumber(_Matrix[_Index]);
        if (!std::isfinite(_Values[_Index]))
            throw std::invalid_argument("Assembly pair mapping has non-finite coordinates");
    }
    constexpr double _RigidTolerance = 1.0e-8;
    if (std::abs(_Values[12]) > _RigidTolerance
        || std::abs(_Values[13]) > _RigidTolerance
        || std::abs(_Values[14]) > _RigidTolerance
        || std::abs(_Values[15] - 1.0) > _RigidTolerance)
        throw std::invalid_argument("Assembly pair mapping is not affine");

    const auto _DotRows = [&_Values](const int Left_, const int Right_) {
        return _Values[Left_ * 4] * _Values[Right_ * 4]
            + _Values[Left_ * 4 + 1] * _Values[Right_ * 4 + 1]
            + _Values[Left_ * 4 + 2] * _Values[Right_ * 4 + 2];
    };
    for (int _Row = 0; _Row < 3; ++_Row)
    {
        if (std::abs(_DotRows(_Row,_Row) - 1.0) > _RigidTolerance)
            throw std::invalid_argument("Assembly pair mapping contains scale or shear");
        for (int _Other = _Row + 1; _Other < 3; ++_Other)
            if (std::abs(_DotRows(_Row,_Other)) > _RigidTolerance)
                throw std::invalid_argument("Assembly pair mapping contains shear");
    }
    const double _Determinant =
        _Values[0] * (_Values[5] * _Values[10] - _Values[6] * _Values[9])
        - _Values[1] * (_Values[4] * _Values[10] - _Values[6] * _Values[8])
        + _Values[2] * (_Values[4] * _Values[9] - _Values[5] * _Values[8]);
    if (std::abs(_Determinant - 1.0) > _RigidTolerance)
        throw std::invalid_argument("Assembly pair mapping is not a proper rotation");

    gp_Trsf _Transform;
    _Transform.SetValues(
        _Values[0],_Values[1],_Values[2],_Values[3],
        _Values[4],_Values[5],_Values[6],_Values[7],
        _Values[8],_Values[9],_Values[10],_Values[11]);
    return _Transform;
}

gp_Trsf AssemblyPairVerifiedPartToSource(const ObjectMap& Mapping_)
{
    if (AssemblyPairMappingString(Mapping_,"status") != "verified"
        || AssemblyPairMappingString(Mapping_,"verification") != "normalized-source-brep-replay")
        throw std::invalid_argument("Assembly pair coordinate mapping is not verified");
    const gp_Trsf _PartToSource = AssemblyPairRigidMatrix(Mapping_,"partToSource");
    const gp_Trsf _SourceToPart = AssemblyPairRigidMatrix(Mapping_,"sourceToPart");
    constexpr double _RoundTripTolerance = 1.0e-6; // mm
    for (const gp_Pnt& _PartPoint : {gp_Pnt(0,0,0),gp_Pnt(1,0,0),
        gp_Pnt(0,1,0),gp_Pnt(0,0,1)})
    {
        if (_PartPoint.Distance(_PartPoint.Transformed(_PartToSource)
            .Transformed(_SourceToPart)) > _RoundTripTolerance)
            throw std::invalid_argument("Assembly pair coordinate matrices disagree");
    }
    return _PartToSource;
}

// A node-local solid distance can locate material contact or separation. It is
// deliberately not a feature-specific clearance measurement. In particular,
// nearestDistanceMm must not be compared with tab/slot fitChecks.clearanceMm.
ObjectMap MeasureAssemblyPairNearNode(const TopoDS_Shape& FinalPartA_,
    const ObjectMap& VerifiedMappingA_, const TopoDS_Shape& FinalPartB_,
    const ObjectMap& VerifiedMappingB_, const gp_Pnt& NodeInSource_,
    const double LocalRadiusMm_)
{
    const auto _Unsupported = [](const std::string& Reason_) {
        return ObjectMap{{"status",std::string("unsupported")},{"reason",Reason_}};
    };
    try
    {
        if (!std::isfinite(NodeInSource_.X()) || !std::isfinite(NodeInSource_.Y())
            || !std::isfinite(NodeInSource_.Z()) || !std::isfinite(LocalRadiusMm_)
            || LocalRadiusMm_ <= 0.0)
            return _Unsupported("Assembly pair node or local radius is invalid");
        const gp_Trsf _PartToSourceA = AssemblyPairVerifiedPartToSource(VerifiedMappingA_);
        const gp_Trsf _PartToSourceB = AssemblyPairVerifiedPartToSource(VerifiedMappingB_);
        if (FinalPartA_.IsNull() || FinalPartB_.IsNull()
            || !BRepCheck_Analyzer(FinalPartA_).IsValid()
            || !BRepCheck_Analyzer(FinalPartB_).IsValid()
            || !TopExp_Explorer(FinalPartA_,TopAbs_SOLID).More()
            || !TopExp_Explorer(FinalPartB_,TopAbs_SOLID).More())
            return _Unsupported("Finished manufacturing BRep is invalid");

        const double _Diameter = 2.0 * LocalRadiusMm_;
        const gp_Pnt _BoxMinimum(NodeInSource_.X() - LocalRadiusMm_,
            NodeInSource_.Y() - LocalRadiusMm_,NodeInSource_.Z() - LocalRadiusMm_);
        const gp_Pnt _BoxMaximum(NodeInSource_.X() + LocalRadiusMm_,
            NodeInSource_.Y() + LocalRadiusMm_,NodeInSource_.Z() + LocalRadiusMm_);
        if (!std::isfinite(_Diameter) || !std::isfinite(_BoxMinimum.X())
            || !std::isfinite(_BoxMinimum.Y()) || !std::isfinite(_BoxMinimum.Z())
            || !std::isfinite(_BoxMaximum.X()) || !std::isfinite(_BoxMaximum.Y())
            || !std::isfinite(_BoxMaximum.Z()))
            return _Unsupported("Assembly pair node-local box is invalid");

        return iCAX::Tasks::Run([&]() -> ObjectMap {
            const auto _PlacedA = BRepBuilderAPI_Transform(
                FinalPartA_,_PartToSourceA,true).Shape();
            const auto _PlacedB = BRepBuilderAPI_Transform(
                FinalPartB_,_PartToSourceB,true).Shape();
            if (_PlacedA.IsNull() || _PlacedB.IsNull()
                || !BRepCheck_Analyzer(_PlacedA).IsValid()
                || !BRepCheck_Analyzer(_PlacedB).IsValid())
                return _Unsupported("Placed finished BRep is invalid");

            const auto _Window = BRepPrimAPI_MakeBox(
                _BoxMinimum,_Diameter,_Diameter,_Diameter).Shape();
            if (_Window.IsNull() || !BRepCheck_Analyzer(_Window).IsValid())
                return _Unsupported("Assembly pair node-local box could not be built");
            const auto _Clip = [&](const TopoDS_Shape& Shape_) {
                BRepAlgoAPI_Common _Common(Shape_,_Window);
                _Common.Build();
                if (!_Common.IsDone() || _Common.Shape().IsNull()
                    || !BRepCheck_Analyzer(_Common.Shape()).IsValid())
                    throw std::runtime_error("Assembly pair node-local material clip failed");
                return _Common.Shape();
            };
            const auto _LocalA = _Clip(_PlacedA);
            const auto _LocalB = _Clip(_PlacedB);
            const auto _SolidVolume = [](const TopoDS_Shape& Shape_) {
                GProp_GProps _Properties;
                BRepGProp::VolumeProperties(Shape_,_Properties);
                return std::abs(_Properties.Mass());
            };
            const double _LocalVolumeA = _SolidVolume(_LocalA);
            const double _LocalVolumeB = _SolidVolume(_LocalB);
            if (!std::isfinite(_LocalVolumeA) || !std::isfinite(_LocalVolumeB)
                || _LocalVolumeA <= 0.0 || _LocalVolumeB <= 0.0
                || !TopExp_Explorer(_LocalA,TopAbs_SOLID).More()
                || !TopExp_Explorer(_LocalB,TopAbs_SOLID).More())
                return _Unsupported("Both finished parts need material inside the node-local box");

            BRepAlgoAPI_Common _Overlap(_LocalA,_LocalB);
            _Overlap.Build();
            if (!_Overlap.IsDone() || _Overlap.Shape().IsNull()
                || !BRepCheck_Analyzer(_Overlap.Shape()).IsValid())
                return _Unsupported("Assembly pair material intersection failed");
            const double _IntersectionVolume = _SolidVolume(_Overlap.Shape());
            if (!std::isfinite(_IntersectionVolume)
                || _IntersectionVolume > std::min(_LocalVolumeA,_LocalVolumeB)
                    + std::max(1.0e-5,std::min(_LocalVolumeA,_LocalVolumeB) * 1.0e-8))
                return _Unsupported("Assembly pair intersection volume is invalid");

            BRepExtrema_DistShapeShape _Nearest(_LocalA,_LocalB);
            _Nearest.Perform();
            if (!_Nearest.IsDone() || _Nearest.NbSolution() == 0)
                return _Unsupported("Assembly pair nearest distance failed");
            const double _Distance = _IntersectionVolume > 0.0
                ? 0.0 : _Nearest.Value();
            if (!std::isfinite(_Distance) || _Distance < 0.0)
                return _Unsupported("Assembly pair nearest distance is invalid");

            return ObjectMap{{"status",std::string("measured")},
                {"scope",std::string("node-local-box")},
                {"nodeInSource",VariantArray{NodeInSource_.X(),NodeInSource_.Y(),
                    NodeInSource_.Z()}},
                {"localRadiusMm",LocalRadiusMm_},
                {"localMaterialVolumeAMm3",_LocalVolumeA},
                {"localMaterialVolumeBMm3",_LocalVolumeB},
                {"intersectionVolumeMm3",_IntersectionVolume},
                {"nearestDistanceMm",_Distance},
                {"interfaceClearanceCertified",false}};
        },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    }
    catch (const std::exception& Error_)
    {
        return _Unsupported(Error_.what());
    }
    catch (...)
    {
        return _Unsupported("Assembly pair OpenCascade measurement failed");
    }
}
