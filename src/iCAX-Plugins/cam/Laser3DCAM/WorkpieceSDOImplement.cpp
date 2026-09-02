#include "pch.h"
#include "WorkpieceSDOImplement.h"

#include "ExtrusionRecognition/ExtrusionRecognitionService.h"
#include "OpenCascadeResourceImport/OpenCascadeBRepReader.h"
#include "ToolpathResourceKeys.h"
#include "ToolpathResources.h"
#include "TopologyPayloadNames.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "ProductContext/IProductContext.h"
#include "RenderInteraction/RenderInteraction.h"
#include "Services/ServiceProvider.h"
#include "WorkpieceResourceKeys.h"


namespace iCAX::CAM::SDO::Internal
{
namespace
{
    constexpr size_t kMaxInlineTopologyPickItems = 10000;
}

    struct SProjectionBounds final
    {
        double MinX = (std::numeric_limits<double>::max)();
        double MaxX = (std::numeric_limits<double>::lowest)();
        double MinY = (std::numeric_limits<double>::max)();
        double MaxY = (std::numeric_limits<double>::lowest)();
        bool Empty = true;

        void Add(IN const iCAX::GeometryData::Point3& Point_)
        {
            MinX = std::min(MinX, Point_.X);
            MaxX = std::max(MaxX, Point_.X);
            MinY = std::min(MinY, Point_.Y);
            MaxY = std::max(MaxY, Point_.Y);
            Empty = false;
        }
    };

    bool _IsTopologyResourceEmpty(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_) noexcept
    {
        return !pTopology_ || (pTopology_->Faces.empty() && pTopology_->Loops.empty() && pTopology_->Edges.empty());
    }

    ObjectMap _MakeWorkpiecePayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_,
        IN iCAX::Project::ISceneContext* pScene_)
    {
        ObjectMap _Workpiece;
        _Workpiece["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Workpiece["name"] = pWorkpiece_ ? pWorkpiece_->GetName() : std::string();
        _Workpiece["quantity"] = pWorkpiece_ ? pWorkpiece_->GetQuantity() : 0ull;
        _Workpiece["sourcePath"] = pWorkpiece_ ? pWorkpiece_->GetSourcePath() : std::string();
        _Workpiece["geometryResourceUrl"] = pWorkpiece_
            ? pWorkpiece_->GetGeometryResourceID()
            : std::string();
        _Workpiece["sectionTypeId"] = pWorkpiece_
            ? pWorkpiece_->GetSectionTypeID()
            : std::string();
        _Workpiece["sectionParameters"] = pWorkpiece_
            ? pWorkpiece_->GetSectionParameters()
            : iCAX::Data::ObjectMap{};
        _Workpiece["length"] = pWorkpiece_ ? pWorkpiece_->GetLength() : 0.0;
        _Workpiece["modelResourceId"] = pWorkpiece_ ? pWorkpiece_->GetModelResourceID() : std::string();
        _Workpiece["brepResourceId"] = pWorkpiece_ ? pWorkpiece_->GetBRepResourceID() : std::string();
        _Workpiece["topologyResourceId"] = pWorkpiece_ ? pWorkpiece_->GetTopologyResourceID() : std::string();
        _Workpiece["topologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetTopologyVersion() : 0ull;
        _Workpiece["geometryRevision"] = pWorkpiece_ ? pWorkpiece_->GetGeometryRevision() : 0ull;
        _Workpiece["editState"] = pWorkpiece_ ? pWorkpiece_->GetEditState() : std::string("Current");
        _Workpiece["hasDraft"] = pWorkpiece_ && !pWorkpiece_->GetDraftBRepResourceID().empty();
        _Workpiece["draftTopologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetDraftTopologyVersion() : 0ull;
        _Workpiece["isLoaded"] = pWorkpiece_
            && !pWorkpiece_->GetGeometryResourceID().empty();
        _Workpiece["tubeNeutralGeometryResourceId"] = pWorkpiece_
            ? pWorkpiece_->GetTubeNeutralGeometryResourceID()
            : std::string();
        const auto _pRender = pEntity_
            ? _GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(pEntity_)
            : nullptr;
        const auto _PreviewResourceURL = _pRender
            ? _pRender->GetGeometryResourceID()
            : std::string();
        _Workpiece["previewResourceUrl"] = _PreviewResourceURL;
        _Workpiece["previewResourceVersion"] = pScene_ && !_PreviewResourceURL.empty()
            ? pScene_->Resources().GetVersion(_PreviewResourceURL)
            : 0ull;
        unsigned long long _BoundaryCount = 0;
        if (pScene_ && pWorkpiece_ && !pWorkpiece_->GetTubeNeutralGeometryResourceID().empty())
        {
            const auto _pGeometry = pScene_->Resources().Get<
                iCAX::GeometryData::Tube::CTubeNeutralGeometry>(
                    pWorkpiece_->GetTubeNeutralGeometryResourceID());
            if (_pGeometry)
            {
                const auto _Base = std::find_if(
                    _pGeometry->SolidNodes.begin(),
                    _pGeometry->SolidNodes.end(),
                    [&_pGeometry](IN const auto& Node_) {
                        return Node_.ID == _pGeometry->BaseNodeID;
                    });
                if (_Base != _pGeometry->SolidNodes.end())
                {
                    if (const auto _pExtrusion = std::get_if<
                        iCAX::GeometryData::Tube::SExtrudedRegionNode>(&_Base->Data))
                    {
                        _BoundaryCount = static_cast<unsigned long long>(
                            _pExtrusion->Section.Boundaries.size());
                    }
                    else if (const auto _pTaper = std::get_if<
                        iCAX::GeometryData::Tube::STaperedRegionNode>(&_Base->Data))
                    {
                        _BoundaryCount = static_cast<unsigned long long>(
                            _pTaper->Section.Boundaries.size());
                    }
                }
            }
        }
        _Workpiece["thumbnailBoundaryCount"] = _BoundaryCount;
        _Workpiece["thumbnailInnerBoundaryCount"] = _BoundaryCount > 0
            ? _BoundaryCount - 1
            : 0ull;
        return _Workpiece;
    }

    VariantArray _MakeWorkpieceArray(IN iCAX::Project::ISceneContext& Scene_)
    {
        VariantArray _Workpieces;
        for (const auto& [_pEntity, _pWorkpiece] : _CollectEntitiesWithComponent<iCAX::CAM::CWorkpieceComponent>(Scene_.Database()))
        {
            _Workpieces.emplace_back(_MakeWorkpiecePayload(_pEntity, _pWorkpiece, &Scene_));
        }
        return _Workpieces;
    }

    ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_)
    {
        ObjectMap _Status;
        _Status["hasTopology"] = !_IsTopologyResourceEmpty(pTopology_);
        _Status["version"] = pTopology_ ? pTopology_->nVersion : 0ull;
        _Status["faceCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Faces.size()) : 0ull;
        _Status["loopCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Loops.size()) : 0ull;
        _Status["edgeCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Edges.size()) : 0ull;
        _Status["inlinePickMapLimit"] = static_cast<unsigned long long>(kMaxInlineTopologyPickItems);
        _Status["inlinePickMapTruncated"] = pTopology_
            && (pTopology_->Faces.size() > kMaxInlineTopologyPickItems
                || pTopology_->Loops.size() > kMaxInlineTopologyPickItems
                || pTopology_->Edges.size() > kMaxInlineTopologyPickItems);
        _Status["metadata"] = pTopology_ ? pTopology_->Metadata : ObjectMap{};

        if (pTopology_)
        {
            auto _ImportMode = pTopology_->Metadata.find("importMode");
            if (_ImportMode != pTopology_->Metadata.end())
            {
                _Status["importMode"] = _ImportMode->second;
            }

            auto _Diagnostic = pTopology_->Metadata.find("diagnostic");
            if (_Diagnostic != pTopology_->Metadata.end())
            {
                _Status["diagnostic"] = _Diagnostic->second;
            }
        }
        return _Status;
    }

    std::string _TubeSolidNodeType(
        IN const iCAX::GeometryData::Tube::SSolidNode& Node_)
    {
        using namespace iCAX::GeometryData::Tube;
        if (std::holds_alternative<SExtrudedRegionNode>(Node_.Data)) return "ExtrudedRegion";
        if (std::holds_alternative<STaperedRegionNode>(Node_.Data)) return "TaperedRegion";
        if (std::holds_alternative<SHalfSpaceNode>(Node_.Data)) return "HalfSpace";
        if (std::holds_alternative<SBooleanNode>(Node_.Data)) return "Boolean";
        if (std::holds_alternative<STransformNode>(Node_.Data)) return "Transform";
        if (std::holds_alternative<SWrappedVolumeNode>(Node_.Data)) return "WrappedVolume";
        if (std::holds_alternative<SOpeningProfileSweepNode>(Node_.Data)) return "OpeningProfileSweep";
        if (std::holds_alternative<SCompositeVolumeNode>(Node_.Data)) return "CompositeVolume";
        if (std::holds_alternative<SResidualBRepNode>(Node_.Data)) return "ResidualBRep";
        if (std::holds_alternative<SAlternativeNode>(Node_.Data)) return "Alternative";
        return "Unknown";
    }

    std::string _TubeMaterialRole(
        IN const iCAX::GeometryData::Tube::SSolidNode& Node_)
    {
        using iCAX::GeometryData::Tube::EFeatureMaterialRole;
        if (!Node_.Relations || !Node_.Relations->MaterialEffect)
        {
            return "None";
        }
        switch (Node_.Relations->MaterialEffect->Role)
        {
        case EFeatureMaterialRole::Penetration: return "Penetration";
        case EFeatureMaterialRole::Truncation: return "Truncation";
        case EFeatureMaterialRole::BoundaryProfileModifier: return "BoundaryProfileModifier";
        case EFeatureMaterialRole::UnclassifiedRemoval: return "UnclassifiedRemoval";
        }
        return "UnclassifiedRemoval";
    }

    VariantArray _MakeTubeNodeChildren(
        IN const iCAX::GeometryData::Tube::SSolidNode& Node_)
    {
        using namespace iCAX::GeometryData::Tube;
        VariantArray _Children;
        if (const auto _pBoolean = std::get_if<SBooleanNode>(&Node_.Data))
        {
            for (const auto& _Child : _pBoolean->Children)
            {
                _Children.emplace_back(_Child);
            }
        }
        else if (const auto _pTransform = std::get_if<STransformNode>(&Node_.Data))
        {
            _Children.emplace_back(_pTransform->Child);
        }
        else if (const auto _pOpening = std::get_if<SOpeningProfileSweepNode>(&Node_.Data))
        {
            _Children.emplace_back(_pOpening->SourceCutNodeID);
            if (!_pOpening->EvaluatedVolumeNodeID.empty())
            {
                _Children.emplace_back(_pOpening->EvaluatedVolumeNodeID);
            }
        }
        else if (const auto _pAlternative = std::get_if<SAlternativeNode>(&Node_.Data))
        {
            for (const auto& _Candidate : _pAlternative->Candidates)
            {
                _Children.emplace_back(_Candidate.CandidateNodeID);
            }
        }
        return _Children;
    }

    void _AppendParameter(
        IN OUT VariantArray& Parameters_,
        IN const std::string& strName_,
        IN const std::string& strLabel_,
        IN double dValue_,
        IN const std::string& strUnit_,
        IN double dStep_,
        IN bool bEditable_)
    {
        ObjectMap _Parameter;
        _Parameter["name"] = strName_;
        _Parameter["label"] = strLabel_;
        _Parameter["value"] = dValue_;
        _Parameter["unit"] = strUnit_;
        _Parameter["step"] = dStep_;
        _Parameter["editable"] = bEditable_;
        Parameters_.emplace_back(std::move(_Parameter));
    }

    void _AppendEditableParameter(
        IN OUT VariantArray& Parameters_,
        IN const std::string& strName_,
        IN const std::string& strLabel_,
        IN double dValue_,
        IN const std::string& strUnit_,
        IN double dStep_)
    {
        _AppendParameter(
            Parameters_,
            strName_,
            strLabel_,
            dValue_,
            strUnit_,
            dStep_,
            true);
    }

    void _AppendDerivedParameter(
        IN OUT VariantArray& Parameters_,
        IN const std::string& strName_,
        IN const std::string& strLabel_,
        IN double dValue_,
        IN const std::string& strUnit_,
        IN double dStep_ = 0.001)
    {
        _AppendParameter(
            Parameters_,
            strName_,
            strLabel_,
            dValue_,
            strUnit_,
            dStep_,
            false);
    }

    void _AppendFrameAxisParameters(
        IN OUT VariantArray& Parameters_,
        IN const iCAX::GeometryData::Direction3& Axis_)
    {
        _AppendDerivedParameter(Parameters_, "axisX", "构造方向 X", Axis_.X, "");
        _AppendDerivedParameter(Parameters_, "axisY", "构造方向 Y", Axis_.Y, "");
        _AppendDerivedParameter(Parameters_, "axisZ", "构造方向 Z", Axis_.Z, "");
    }

    ObjectMap _MakeTubeSolidNodePayload(
        IN const iCAX::GeometryData::Tube::SSolidNode& Node_)
    {
        using namespace iCAX::GeometryData::Tube;
        ObjectMap _Payload;
        _Payload["id"] = Node_.ID;
        _Payload["label"] = Node_.Label;
        _Payload["type"] = _TubeSolidNodeType(Node_);
        _Payload["materialRole"] = _TubeMaterialRole(Node_);
        _Payload["children"] = _MakeTubeNodeChildren(Node_);
        _Payload["previewAvailable"] =
            Node_.Metadata.find("previewResourceUrl") != Node_.Metadata.end();
        ObjectMap _Metadata;
        for (const auto& [_Key, _Value] : Node_.Metadata)
        {
            _Metadata[_Key] = _Value;
        }
        _Payload["metadata"] = std::move(_Metadata);
        VariantArray _Parameters;

        if (const auto _pExtrusion = std::get_if<SExtrudedRegionNode>(&Node_.Data))
        {
            _AppendEditableParameter(_Parameters, "first", "起点", _pExtrusion->First, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "last", "终点", _pExtrusion->Last, "mm", 1.0);
            _AppendEditableParameter(
                _Parameters,
                "length",
                "拉伸长度",
                _pExtrusion->Last - _pExtrusion->First,
                "mm",
                1.0);
            _AppendEditableParameter(_Parameters, "locationX", "位置 X", _pExtrusion->Frame.Location.X, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "locationY", "位置 Y", _pExtrusion->Frame.Location.Y, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "locationZ", "位置 Z", _pExtrusion->Frame.Location.Z, "mm", 1.0);
            _AppendFrameAxisParameters(_Parameters, _pExtrusion->Frame.ZDirection);
            _Payload["sectionBoundaryCount"] = static_cast<unsigned long long>(
                _pExtrusion->Section.Boundaries.size());
            _Payload["sideAtlasCount"] = static_cast<unsigned long long>(
                _pExtrusion->SideAtlases.size());
        }
        else if (const auto _pTaper = std::get_if<STaperedRegionNode>(&Node_.Data))
        {
            _AppendEditableParameter(_Parameters, "first", "起点", _pTaper->First, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "last", "终点", _pTaper->Last, "mm", 1.0);
            _AppendEditableParameter(
                _Parameters,
                "length",
                "渐缩长度",
                _pTaper->Last - _pTaper->First,
                "mm",
                1.0);
            _AppendEditableParameter(_Parameters, "firstScale", "起始比例", _pTaper->FirstScale, "", 0.01);
            _AppendEditableParameter(_Parameters, "lastScale", "结束比例", _pTaper->LastScale, "", 0.01);
            _AppendEditableParameter(_Parameters, "locationX", "位置 X", _pTaper->Frame.Location.X, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "locationY", "位置 Y", _pTaper->Frame.Location.Y, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "locationZ", "位置 Z", _pTaper->Frame.Location.Z, "mm", 1.0);
            _AppendFrameAxisParameters(_Parameters, _pTaper->Frame.ZDirection);
            _Payload["sectionBoundaryCount"] = static_cast<unsigned long long>(
                _pTaper->Section.Boundaries.size());
        }
        else if (const auto _pHalfSpace = std::get_if<SHalfSpaceNode>(&Node_.Data))
        {
            _AppendEditableParameter(_Parameters, "locationX", "平面位置 X", _pHalfSpace->Boundary.Location.X, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "locationY", "平面位置 Y", _pHalfSpace->Boundary.Location.Y, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "locationZ", "平面位置 Z", _pHalfSpace->Boundary.Location.Z, "mm", 1.0);
            _AppendEditableParameter(_Parameters, "normalX", "法向 X", _pHalfSpace->Boundary.Normal.X, "", 0.01);
            _AppendEditableParameter(_Parameters, "normalY", "法向 Y", _pHalfSpace->Boundary.Normal.Y, "", 0.01);
            _AppendEditableParameter(_Parameters, "normalZ", "法向 Z", _pHalfSpace->Boundary.Normal.Z, "", 0.01);
        }
        else if (const auto _pWrapped = std::get_if<SWrappedVolumeNode>(&Node_.Data))
        {
            if (const auto _pLower = std::get_if<double>(&_pWrapped->LowerNormalOffset))
            {
                _AppendEditableParameter(_Parameters, "lowerNormalOffset", "法向下界", *_pLower, "mm", 0.1);
            }
            if (const auto _pUpper = std::get_if<double>(&_pWrapped->UpperNormalOffset))
            {
                _AppendEditableParameter(_Parameters, "upperNormalOffset", "法向上界", *_pUpper, "mm", 0.1);
            }
            _Payload["supportLoopId"] = _pWrapped->Support.LoopID;
            _Payload["supportCurveId"] = _pWrapped->Support.CurveID;
            _Payload["uvBoundaryCount"] = static_cast<unsigned long long>(
                _pWrapped->UVRegion.Boundaries.size());
        }
        else if (const auto _pOpening = std::get_if<SOpeningProfileSweepNode>(&Node_.Data))
        {
            _Payload["profileKeyCount"] = static_cast<unsigned long long>(
                _pOpening->ProfileField.Keys.size());
            _Payload["sourceCutNodeId"] = _pOpening->SourceCutNodeID;
            _AppendDerivedParameter(
                _Parameters,
                "contourFirst",
                "轮廓起点",
                _pOpening->ProfileField.ContourFirst,
                "mm");
            _AppendDerivedParameter(
                _Parameters,
                "contourLast",
                "轮廓终点",
                _pOpening->ProfileField.ContourLast,
                "mm");
            if (!_pOpening->ProfileField.Keys.empty()
                && !_pOpening->ProfileField.Keys.front().Profile.Segments.empty())
            {
                const auto& _Curve = _pOpening->ProfileField.Keys.front()
                    .Profile.Segments.front().Curve;
                if (const auto _pSegment =
                    std::get_if<iCAX::GeometryData::Segment2>(&_Curve))
                {
                    _AppendEditableParameter(
                        _Parameters, "profileStart", "剖面起点", _pSegment->Start.X, "mm", 0.1);
                    _AppendEditableParameter(
                        _Parameters, "profileEnd", "剖面终点", _pSegment->End.X, "mm", 0.1);
                    _AppendEditableParameter(
                        _Parameters, "startOffset", "起点减材偏移", _pSegment->Start.Y, "mm", 0.1);
                    _AppendEditableParameter(
                        _Parameters, "endOffset", "终点减材偏移", _pSegment->End.Y, "mm", 0.1);
                }
            }
            const auto _Angle = _pOpening->ProfileField.Metadata.find(
                "derivedSemiAngleRadians");
            if (_Angle != _pOpening->ProfileField.Metadata.end())
            {
                try
                {
                    _AppendDerivedParameter(
                        _Parameters,
                        "derivedBevelAngle",
                        "派生坡口角",
                        std::stod(_Angle->second) * 180.0 / 3.14159265358979323846,
                        "°",
                        0.1);
                }
                catch (const std::exception&)
                {
                }
            }
            const auto _Land = _pOpening->ProfileField.Metadata.find("derivedLand");
            if (_Land != _pOpening->ProfileField.Metadata.end())
            {
                try
                {
                    _AppendDerivedParameter(
                        _Parameters,
                        "derivedLand",
                        "派生钝边",
                        std::stod(_Land->second),
                        "mm",
                        0.1);
                }
                catch (const std::exception&)
                {
                }
            }
        }
        else if (const auto _pAlternative = std::get_if<SAlternativeNode>(&Node_.Data))
        {
            VariantArray _Candidates;
            for (const auto& _Source : _pAlternative->Candidates)
            {
                ObjectMap _Candidate;
                _Candidate["id"] = _Source.ID;
                _Candidate["label"] = _Source.Label;
                _Candidate["nodeId"] = _Source.CandidateNodeID;
                _Candidate["confidence"] = _Source.Confidence;
                _Candidate["geometryError"] = _Source.RelativeGeometryError;
                _Candidate["recommendationScore"] = _Source.Evaluation.RecommendationScore;
                _Candidates.emplace_back(std::move(_Candidate));
            }
            _Payload["candidates"] = std::move(_Candidates);
            _Payload["recommendedCandidateId"] = _pAlternative->RecommendedCandidateID;
            _Payload["selectedCandidateId"] = _pAlternative->SelectedCandidateID;
        }
        _Payload["parameters"] = std::move(_Parameters);
        return _Payload;
    }

    std::string _SectionPrimitiveKind(
        IN iCAX::GeometryData::Tube::ESectionPrimitiveKind Kind_)
    {
        using iCAX::GeometryData::Tube::ESectionPrimitiveKind;
        switch (Kind_)
        {
        case ESectionPrimitiveKind::Circle: return "Circle";
        case ESectionPrimitiveKind::Rectangle: return "Rectangle";
        case ESectionPrimitiveKind::CurveLoop: return "CurveLoop";
        }
        return "CurveLoop";
    }

    ObjectMap _MakeSectionPrimitivePayload(
        IN const iCAX::GeometryData::Tube::SSectionPrimitive& Primitive_)
    {
        using iCAX::GeometryData::Tube::ESectionPrimitiveRole;
        ObjectMap _Payload;
        _Payload["id"] = Primitive_.ID;
        _Payload["label"] = Primitive_.Label;
        _Payload["type"] = Primitive_.Role == ESectionPrimitiveRole::OuterBoundary
            ? std::string("SectionOuter")
            : std::string("SectionCavity");
        _Payload["primitiveKind"] = _SectionPrimitiveKind(Primitive_.Kind);
        _Payload["materialRole"] = std::string("None");
        _Payload["group"] = std::string("stock");
        _Payload["loopId"] = Primitive_.LoopID;
        _Payload["children"] = VariantArray{};
        _Payload["previewAvailable"] =
            Primitive_.Metadata.find("previewResourceUrl")
                != Primitive_.Metadata.end();
        ObjectMap _Metadata;
        for (const auto& [_Key, _Value] : Primitive_.Metadata)
        {
            _Metadata[_Key] = _Value;
        }
        _Payload["metadata"] = std::move(_Metadata);
        VariantArray _Parameters;
        _AppendEditableParameter(
            _Parameters, "centerX", "截面中心 X",
            Primitive_.Center.X, "mm", 0.1);
        _AppendEditableParameter(
            _Parameters, "centerY", "截面中心 Y",
            Primitive_.Center.Y, "mm", 0.1);
        if (Primitive_.Kind
            == iCAX::GeometryData::Tube::ESectionPrimitiveKind::Rectangle)
        {
            if (Primitive_.Width)
            {
                _AppendEditableParameter(
                    _Parameters, "width", "宽度",
                    Primitive_.Width->Value, "mm", 0.1);
            }
            if (Primitive_.Height)
            {
                _AppendEditableParameter(
                    _Parameters, "height", "高度",
                    Primitive_.Height->Value, "mm", 0.1);
            }
            _AppendEditableParameter(
                _Parameters, "rotationRadians", "截面旋转",
                Primitive_.RotationRadians, "rad", 0.01);
        }
        else if (Primitive_.Kind
            == iCAX::GeometryData::Tube::ESectionPrimitiveKind::Circle
            && Primitive_.Radius)
        {
            _AppendEditableParameter(
                _Parameters, "radius", "半径",
                Primitive_.Radius->Value, "mm", 0.1);
        }
        _Payload["parameters"] = std::move(_Parameters);
        return _Payload;
    }

    ObjectMap _MakeTubeNeutralGeometryPayload(
        IN const std::string& strResourceID_,
        IN const std::shared_ptr<iCAX::GeometryData::Tube::CTubeNeutralGeometry>& pGeometry_)
    {
        using iCAX::GeometryData::Tube::ERecognitionStatus;
        ObjectMap _Payload;
        _Payload["available"] = static_cast<bool>(pGeometry_);
        _Payload["resourceId"] = strResourceID_;
        _Payload["version"] = pGeometry_ ? pGeometry_->Version : 0ull;
        _Payload["confidence"] = pGeometry_ ? pGeometry_->Confidence : 0.0;
        _Payload["relativeVolumeError"] = pGeometry_ ? pGeometry_->RelativeVolumeError : 1.0;
        _Payload["relativeUnparameterizedVolume"] = pGeometry_
            ? pGeometry_->RelativeUnparameterizedVolume
            : 1.0;
        _Payload["relativeOpaqueResidualVolume"] = pGeometry_
            ? pGeometry_->RelativeOpaqueResidualVolume
            : 1.0;
        _Payload["baseNodeId"] = pGeometry_ ? pGeometry_->BaseNodeID : std::string();
        _Payload["rootNodeId"] = pGeometry_ ? pGeometry_->RootNodeID : std::string();
        _Payload["solidNodeCount"] = pGeometry_
            ? static_cast<unsigned long long>(pGeometry_->SolidNodes.size())
            : 0ull;
        _Payload["uvFeatureCount"] = pGeometry_
            ? static_cast<unsigned long long>(pGeometry_->UVFeatures.size())
            : 0ull;
        VariantArray _SolidNodes;
        if (pGeometry_)
        {
            _SolidNodes.reserve(pGeometry_->SolidNodes.size());
            for (const auto& _Node : pGeometry_->SolidNodes)
            {
                _SolidNodes.emplace_back(_MakeTubeSolidNodePayload(_Node));
            }
        }
        _Payload["solidNodes"] = std::move(_SolidNodes);
        VariantArray _SectionPrimitives;
        if (pGeometry_)
        {
            for (const auto& _Node : pGeometry_->SolidNodes)
            {
                if (_Node.ID != pGeometry_->BaseNodeID)
                {
                    continue;
                }
                if (const auto* _pExtrusion = std::get_if<
                    iCAX::GeometryData::Tube::SExtrudedRegionNode>(&_Node.Data))
                {
                    for (const auto& _Primitive : _pExtrusion->SectionPrimitives)
                    {
                        _SectionPrimitives.emplace_back(
                            _MakeSectionPrimitivePayload(_Primitive));
                    }
                }
                else if (const auto* _pTaper = std::get_if<
                    iCAX::GeometryData::Tube::STaperedRegionNode>(&_Node.Data))
                {
                    for (const auto& _Primitive : _pTaper->SectionPrimitives)
                    {
                        _SectionPrimitives.emplace_back(
                            _MakeSectionPrimitivePayload(_Primitive));
                    }
                }
                break;
            }
        }
        _Payload["sectionPrimitives"] = std::move(_SectionPrimitives);

        auto _Status = std::string("Unavailable");
        if (pGeometry_)
        {
            switch (pGeometry_->RecognitionStatus)
            {
            case ERecognitionStatus::Exact: _Status = "Exact"; break;
            case ERecognitionStatus::EquivalentButAmbiguous: _Status = "EquivalentButAmbiguous"; break;
            case ERecognitionStatus::Partial: _Status = "Partial"; break;
            case ERecognitionStatus::Failed: _Status = "Failed"; break;
            }
        }
        _Payload["status"] = _Status;

        unsigned long long _BoundaryCount = 0;
        unsigned long long _InnerBoundaryCount = 0;
        unsigned long long _AlternativeCount = 0;
        unsigned long long _UnresolvedAlternativeCount = 0;
        unsigned long long _CompositeCount = 0;
        unsigned long long _SurfaceEvidenceCount = 0;
        unsigned long long _MaterialSpanCount = 0;
        if (pGeometry_)
        {
            for (const auto& _Node : pGeometry_->SolidNodes)
            {
                if (_Node.Relations)
                {
                    _SurfaceEvidenceCount += static_cast<unsigned long long>(
                        _Node.Relations->SurfaceEvidence.size());
                    _MaterialSpanCount += static_cast<unsigned long long>(
                        _Node.Relations->MaterialSpans.size());
                }
                if (std::holds_alternative<
                    iCAX::GeometryData::Tube::SCompositeVolumeNode>(_Node.Data))
                {
                    ++_CompositeCount;
                }
                const auto _pAlternative = std::get_if<
                    iCAX::GeometryData::Tube::SAlternativeNode>(&_Node.Data);
                if (!_pAlternative)
                {
                    continue;
                }
                ++_AlternativeCount;
                if (_pAlternative->SelectedCandidateID.empty())
                {
                    ++_UnresolvedAlternativeCount;
                }
            }
            auto _BaseIter = std::find_if(
                pGeometry_->SolidNodes.begin(),
                pGeometry_->SolidNodes.end(),
                [&pGeometry_](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
                    return Node_.ID == pGeometry_->BaseNodeID;
                });
            if (_BaseIter != pGeometry_->SolidNodes.end()
                && std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(_BaseIter->Data))
            {
                const auto& _Base = std::get<iCAX::GeometryData::Tube::SExtrudedRegionNode>(_BaseIter->Data);
                _BoundaryCount = static_cast<unsigned long long>(_Base.Section.Boundaries.size());
                _InnerBoundaryCount = _BoundaryCount > 0 ? _BoundaryCount - 1 : 0;
                _Payload["extrusionLength"] = std::max(0.0, _Base.Last - _Base.First);
                _Payload["axis"] = VariantArray{
                    _Base.Frame.ZDirection.X,
                    _Base.Frame.ZDirection.Y,
                    _Base.Frame.ZDirection.Z
                };
            }
        }
        _Payload["boundaryCount"] = _BoundaryCount;
        _Payload["innerBoundaryCount"] = _InnerBoundaryCount;
        _Payload["alternativeCount"] = _AlternativeCount;
        _Payload["unresolvedAlternativeCount"] = _UnresolvedAlternativeCount;
        _Payload["compositeCount"] = _CompositeCount;
        _Payload["surfaceEvidenceCount"] = _SurfaceEvidenceCount;
        _Payload["materialSpanCount"] = _MaterialSpanCount;

        VariantArray _Diagnostics;
        if (pGeometry_)
        {
            for (const auto& _Source : pGeometry_->Diagnostics)
            {
                ObjectMap _Diagnostic;
                _Diagnostic["code"] = _Source.Code;
                _Diagnostic["message"] = _Source.Message;
                _Diagnostic["confidence"] = _Source.Confidence;
                _Diagnostics.emplace_back(_Diagnostic);
            }
        }
        _Payload["diagnostics"] = _Diagnostics;
        return _Payload;
    }

    ObjectMap _MakeTopologyPickItemPayload(IN const Variant& Item_)
    {
        if (!Item_.Is<ObjectMap>())
        {
            return ObjectMap{};
        }

        const auto _Source = Item_.To<ObjectMap>();
        ObjectMap _Item;
        _Item["id"] = _GetObjectUInt64(_Source, "id");
        _Item["kind"] = _GetObjectString(_Source, "kind");
        _Item["label"] = _GetObjectString(_Source, "label");
        _Item["role"] = _GetObjectString(_Source, "role");
        _Item["triangleStart"] = _GetObjectUInt64(_Source, "triangleStart");
        _Item["triangleCount"] = _GetObjectUInt64(_Source, "triangleCount");
        return _Item;
    }

    VariantArray _MakeTopologyPickArray(IN const VariantArray& Items_)
    {
        VariantArray _Result;
        if (Items_.size() > kMaxInlineTopologyPickItems)
        {
            return _Result;
        }

        _Result.reserve(Items_.size());
        for (const auto& _Item : Items_)
        {
            _Result.emplace_back(_MakeTopologyPickItemPayload(_Item));
        }
        return _Result;
    }

    template <typename TRecord>
    const TRecord* _FindRecordByID(IN const std::vector<TRecord>& Records_, IN uint64_t nID_) noexcept
    {
        auto _Ite = std::find_if(Records_.begin(), Records_.end(), [nID_](IN const TRecord& Record_) {
            return Record_.Id == nID_;
        });
        return _Ite == Records_.end() ? nullptr : &(*_Ite);
    }

    iCAX::GeometryData::Point3 _AddScaled(
        IN const iCAX::GeometryData::Point3& Origin_,
        IN const iCAX::GeometryData::Direction3& Direction_,
        IN double dScale_)
    {
        return {
            Origin_.X + Direction_.X * dScale_,
            Origin_.Y + Direction_.Y * dScale_,
            Origin_.Z + Direction_.Z * dScale_
        };
    }

    iCAX::GeometryData::Point3 _EllipsePoint(
        IN const iCAX::GeometryData::Placement3& Placement_,
        IN double dMajorRadius_,
        IN double dMinorRadius_,
        IN double dAngle_)
    {
        const auto _Cos = std::cos(dAngle_);
        const auto _Sin = std::sin(dAngle_);
        return {
            Placement_.Location.X + Placement_.XDirection.X * dMajorRadius_ * _Cos + Placement_.YDirection.X * dMinorRadius_ * _Sin,
            Placement_.Location.Y + Placement_.XDirection.Y * dMajorRadius_ * _Cos + Placement_.YDirection.Y * dMinorRadius_ * _Sin,
            Placement_.Location.Z + Placement_.XDirection.Z * dMajorRadius_ * _Cos + Placement_.YDirection.Z * dMinorRadius_ * _Sin
        };
    }

    std::pair<double, double> _ResolveCurveRange(IN const iCAX::GeometryData::ParameterRange& Range_, IN double dDefaultFirst_, IN double dDefaultLast_)
    {
        const bool _HasFiniteRange = std::isfinite(Range_.First) && std::isfinite(Range_.Last);
        if (_HasFiniteRange && Range_.First != Range_.Last)
        {
            return { Range_.First, Range_.Last };
        }
        return { dDefaultFirst_, dDefaultLast_ };
    }

    template <typename TPoint>
    void _AppendControlPoints(IN OUT std::vector<iCAX::GeometryData::Point3>& Points_, IN const std::vector<TPoint>& Poles_)
    {
        for (const auto& _Pole : Poles_)
        {
            Points_.push_back({ _Pole.X, _Pole.Y, _Pole.Z });
        }
    }

    std::vector<iCAX::GeometryData::Point3> _SampleCurve3(
        IN const iCAX::GeometryData::Curve3& Curve_,
        IN const iCAX::GeometryData::ParameterRange& Range_)
    {
        using namespace iCAX::GeometryData;

        return std::visit([&Range_](IN const auto& CurveValue_) {
            using TCurve = std::decay_t<decltype(CurveValue_)>;
            std::vector<Point3> _Points;

            if constexpr (std::is_same_v<TCurve, Segment3>)
            {
                _Points.push_back(CurveValue_.Start);
                _Points.push_back(CurveValue_.End);
            }
            else if constexpr (std::is_same_v<TCurve, Line3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, 0.0, 1.0);
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _First));
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _Last));
            }
            else if constexpr (std::is_same_v<TCurve, Ray3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, CurveValue_.First, CurveValue_.First + 1.0);
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _First));
                _Points.push_back(_AddScaled(CurveValue_.Axis.Location, CurveValue_.Axis.Direction, _Last));
            }
            else if constexpr (std::is_same_v<TCurve, Circle3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, 0.0, 6.28318530717958647692);
                constexpr int _SampleCount = 32;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Placement, CurveValue_.Radius, CurveValue_.Radius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, Arc3>)
            {
                const auto _First = CurveValue_.Basis.Radius <= 0.0 ? 0.0 : CurveValue_.StartAngle;
                const auto _Last = CurveValue_.Basis.Radius <= 0.0 ? 0.0 : CurveValue_.EndAngle;
                constexpr int _SampleCount = 16;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Basis.Placement, CurveValue_.Basis.Radius, CurveValue_.Basis.Radius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, Ellipse3>)
            {
                const auto [_First, _Last] = _ResolveCurveRange(Range_, 0.0, 6.28318530717958647692);
                constexpr int _SampleCount = 32;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = _First + (_Last - _First) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Placement, CurveValue_.MajorRadius, CurveValue_.MinorRadius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, EllipseArc3>)
            {
                constexpr int _SampleCount = 16;
                for (int _Index = 0; _Index <= _SampleCount; ++_Index)
                {
                    const auto _T = CurveValue_.StartAngle + (CurveValue_.EndAngle - CurveValue_.StartAngle) * static_cast<double>(_Index) / static_cast<double>(_SampleCount);
                    _Points.push_back(_EllipsePoint(CurveValue_.Basis.Placement, CurveValue_.Basis.MajorRadius, CurveValue_.Basis.MinorRadius, _T));
                }
            }
            else if constexpr (std::is_same_v<TCurve, Polyline3>)
            {
                _Points = CurveValue_.Points;
            }
            else if constexpr (std::is_same_v<TCurve, Bezier3>)
            {
                _AppendControlPoints(_Points, CurveValue_.Poles);
            }
            else if constexpr (std::is_same_v<TCurve, BSpline3>)
            {
                _AppendControlPoints(_Points, CurveValue_.Poles);
            }
            else if constexpr (std::is_same_v<TCurve, NURBS3>)
            {
                _AppendControlPoints(_Points, CurveValue_.Poles);
            }
            else if constexpr (std::is_same_v<TCurve, Clothoid3>)
            {
                _Points.push_back(CurveValue_.Placement.Location);
                _Points.push_back(_AddScaled(CurveValue_.Placement.Location, CurveValue_.Placement.XDirection, CurveValue_.Length));
            }

            return _Points;
        }, Curve_);
    }

    const iCAX::GeometryData::BRepVertex* _FindVertex(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN uint64_t nVertexID_) noexcept
    {
        return _FindRecordByID(Model_.Vertices, nVertexID_);
    }

    std::vector<iCAX::GeometryData::Point3> _SampleEdgePoints(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepEdge& Edge_)
    {
        if (const auto* _pCurve = _FindRecordByID(Model_.Curves3, Edge_.Curve3Id))
        {
            auto _Points = _SampleCurve3(_pCurve->Geometry, Edge_.Range);
            if (!_Points.empty())
            {
                return _Points;
            }
        }

        std::vector<iCAX::GeometryData::Point3> _Points;
        if (const auto* _pStart = _FindVertex(Model_, Edge_.StartVertexId))
        {
            _Points.push_back(_pStart->Position);
        }
        if (const auto* _pEnd = _FindVertex(Model_, Edge_.EndVertexId))
        {
            _Points.push_back(_pEnd->Position);
        }
        return _Points;
    }

    std::vector<iCAX::GeometryData::Point3> _CollectWirePoints(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepWire& Wire_)
    {
        std::vector<iCAX::GeometryData::Point3> _Points;
        for (const auto& _Coedge : Wire_.Coedges)
        {
            if (const auto* _pEdge = _FindRecordByID(Model_.Edges, _Coedge.EdgeId))
            {
                auto _EdgePoints = _SampleEdgePoints(Model_, *_pEdge);
                _Points.insert(_Points.end(), _EdgePoints.begin(), _EdgePoints.end());
            }
        }
        return _Points;
    }

    void _CollectBRepBounds(IN const iCAX::GeometryData::BRepModel& Model_, IN OUT SProjectionBounds& Bounds_)
    {
        for (const auto& _Vertex : Model_.Vertices)
        {
            Bounds_.Add(_Vertex.Position);
        }
        for (const auto& _Triangulation : Model_.Triangulations3)
        {
            for (const auto& _Point : _Triangulation.Geometry.Vertices)
            {
                Bounds_.Add(_Point);
            }
        }
        for (const auto& _Edge : Model_.Edges)
        {
            for (const auto& _Point : _SampleEdgePoints(Model_, _Edge))
            {
                Bounds_.Add(_Point);
            }
        }
    }

    Variant _MakeProjectedPoint(IN double dX_, IN double dY_)
    {
        ObjectMap _Point;
        _Point["x"] = dX_;
        _Point["y"] = dY_;
        return Variant(_Point);
    }

    VariantArray _MakeProjectedPointArray(IN const std::vector<iCAX::GeometryData::Point3>& Points_, IN const SProjectionBounds& Bounds_)
    {
        const double _Width = std::max(1.0, Bounds_.MaxX - Bounds_.MinX);
        const double _Height = std::max(1.0, Bounds_.MaxY - Bounds_.MinY);
        const double _Scale = std::min(700.0 / _Width, 340.0 / _Height);
        const double _OffsetX = 40.0 + (700.0 - _Width * _Scale) * 0.5;
        const double _OffsetY = 40.0 + (340.0 - _Height * _Scale) * 0.5;

        VariantArray _Result;
        _Result.reserve(Points_.size());
        for (const auto& _Point : Points_)
        {
            const double _X = _OffsetX + (_Point.X - Bounds_.MinX) * _Scale;
            const double _Y = _OffsetY + (Bounds_.MaxY - _Point.Y) * _Scale;
            _Result.emplace_back(_MakeProjectedPoint(_X, _Y));
        }
        return _Result;
    }

    ObjectMap _MakeProjectedEdge(
        IN uint64_t nID_,
        IN const std::string& strLabel_,
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const SProjectionBounds& Bounds_)
    {
        ObjectMap _Edge;
        _Edge["id"] = static_cast<unsigned long long>(nID_);
        _Edge["kind"] = std::string(kTopologyKindEdge);
        _Edge["label"] = strLabel_;
        _Edge["role"] = std::string("cad-edge");
        _Edge["points"] = _MakeProjectedPointArray(Points_, Bounds_);
        return _Edge;
    }

    ObjectMap _MakeProjectedFace(
        IN uint64_t nID_,
        IN const std::string& strLabel_,
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const SProjectionBounds& Bounds_,
        IN uint64_t nTriangleStart_,
        IN uint64_t nTriangleCount_)
    {
        ObjectMap _Face;
        _Face["id"] = static_cast<unsigned long long>(nID_);
        _Face["kind"] = std::string(kTopologyKindFace);
        _Face["label"] = strLabel_;
        _Face["role"] = std::string("cad-face");
        _Face["points"] = _MakeProjectedPointArray(Points_, Bounds_);
        if (nTriangleCount_ > 0)
        {
            _Face["triangleStart"] = static_cast<unsigned long long>(nTriangleStart_);
            _Face["triangleCount"] = static_cast<unsigned long long>(nTriangleCount_);
        }
        return _Face;
    }

    ObjectMap _MakeProjectedLoop(
        IN uint64_t nID_,
        IN const std::string& strLabel_,
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const SProjectionBounds& Bounds_,
        IN const std::string& strRole_)
    {
        SProjectionBounds _LoopBounds;
        for (const auto& _Point : Points_)
        {
            _LoopBounds.Add(_Point);
        }
        if (_LoopBounds.Empty)
        {
            _LoopBounds = Bounds_;
        }

        const auto _CenterPoints = _MakeProjectedPointArray(
            { { (_LoopBounds.MinX + _LoopBounds.MaxX) * 0.5, (_LoopBounds.MinY + _LoopBounds.MaxY) * 0.5, 0.0 } },
            Bounds_);
        const auto _RadiusPoints = _MakeProjectedPointArray(
            { { _LoopBounds.MaxX, (_LoopBounds.MinY + _LoopBounds.MaxY) * 0.5, 0.0 } },
            Bounds_);

        double _CenterX = 0.0;
        double _CenterY = 0.0;
        double _RadiusX = 0.0;
        if (!_CenterPoints.empty())
        {
            const auto _Center = _CenterPoints.front().To<ObjectMap>();
            _CenterX = _Center.at("x").To<double>();
            _CenterY = _Center.at("y").To<double>();
        }
        if (!_RadiusPoints.empty())
        {
            const auto _RadiusPoint = _RadiusPoints.front().To<ObjectMap>();
            _RadiusX = _RadiusPoint.at("x").To<double>();
        }

        ObjectMap _Loop;
        _Loop["id"] = static_cast<unsigned long long>(nID_);
        _Loop["kind"] = std::string(kTopologyKindLoop);
        _Loop["label"] = strLabel_;
        _Loop["role"] = strRole_;
        _Loop["center"] = _MakeProjectedPoint(_CenterX, _CenterY);
        _Loop["radius"] = std::max(6.0, std::abs(_RadiusX - _CenterX));
        return _Loop;
    }

    std::vector<iCAX::GeometryData::Point3> _MakeFacePreviewPolygon(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepFace& Face_)
    {
        SProjectionBounds _Bounds;
        if (const auto* _pTriangulation = _FindRecordByID(Model_.Triangulations3, Face_.Triangulation3Id))
        {
            for (const auto& _Point : _pTriangulation->Geometry.Vertices)
            {
                _Bounds.Add(_Point);
            }
        }

        if (_Bounds.Empty)
        {
            for (const auto _WireID : Face_.WireIds)
            {
                if (const auto* _pWire = _FindRecordByID(Model_.Wires, _WireID))
                {
                    for (const auto& _Point : _CollectWirePoints(Model_, *_pWire))
                    {
                        _Bounds.Add(_Point);
                    }
                }
            }
        }

        if (_Bounds.Empty)
        {
            return {};
        }

        return {
            { _Bounds.MinX, _Bounds.MinY, 0.0 },
            { _Bounds.MaxX, _Bounds.MinY, 0.0 },
            { _Bounds.MaxX, _Bounds.MaxY, 0.0 },
            { _Bounds.MinX, _Bounds.MaxY, 0.0 }
        };
    }

    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> _BuildTriangulationTriangleRanges(
        IN const iCAX::GeometryData::BRepModel& Model_)
    {
        std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> _Ranges;
        uint64_t _TriangleStart = 0;
        for (const auto& _TriangulationRecord : Model_.Triangulations3)
        {
            const auto& _Geometry = _TriangulationRecord.Geometry;
            if (_Geometry.Vertices.empty() || _Geometry.Triangles.empty())
            {
                continue;
            }

            const auto _TriangleCount = static_cast<uint64_t>(_Geometry.Triangles.size());
            _Ranges[_TriangulationRecord.Id] = { _TriangleStart, _TriangleCount };
            _TriangleStart += _TriangleCount;
        }
        return _Ranges;
    }

    std::unordered_map<uint64_t, std::string> _BuildWireRoles(IN const iCAX::GeometryData::BRepModel& Model_)
    {
        std::unordered_map<uint64_t, std::string> _Roles;
        for (const auto& _Face : Model_.Faces)
        {
            for (size_t _Index = 0; _Index < _Face.WireIds.size(); ++_Index)
            {
                const auto _WireID = _Face.WireIds[_Index];
                if (_WireID == 0)
                {
                    continue;
                }

                const auto _Role = _Index == 0 ? std::string("cut") : std::string("hole");
                auto _Iter = _Roles.find(_WireID);
                if (_Iter == _Roles.end() || _Role == "hole")
                {
                    _Roles[_WireID] = _Role;
                }
            }
        }
        return _Roles;
    }

    std::shared_ptr<iCAX::CAM::CTopologyResource> _MakeTopologyResourceFromBRep(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const std::string& strDisplayName_,
        IN uint64_t nTopologyVersion_)
    {
        SProjectionBounds _Bounds;
        _CollectBRepBounds(Model_, _Bounds);
        if (_Bounds.Empty)
        {
            _Bounds.Add({ 0.0, 0.0, 0.0 });
            _Bounds.Add({ 1.0, 1.0, 0.0 });
        }

        auto _pTopology = std::make_shared<iCAX::CAM::CTopologyResource>();
        _pTopology->nVersion = nTopologyVersion_;
        const auto _TriangleRanges = _BuildTriangulationTriangleRanges(Model_);
        const auto _WireRoles = _BuildWireRoles(Model_);

        for (const auto& _Face : Model_.Faces)
        {
            auto _Polygon = _MakeFacePreviewPolygon(Model_, _Face);
            if (!_Polygon.empty())
            {
                const auto _RangeIter = _TriangleRanges.find(_Face.Triangulation3Id);
                const auto _TriangleStart = _RangeIter == _TriangleRanges.end() ? 0ull : _RangeIter->second.first;
                const auto _TriangleCount = _RangeIter == _TriangleRanges.end() ? 0ull : _RangeIter->second.second;
                _pTopology->Faces.emplace_back(_MakeProjectedFace(
                    _Face.Id,
                    strDisplayName_ + " face " + std::to_string(_Face.Id),
                    _Polygon,
                    _Bounds,
                    _TriangleStart,
                    _TriangleCount));
            }
        }

        for (const auto& _Wire : Model_.Wires)
        {
            auto _LoopPoints = _CollectWirePoints(Model_, _Wire);
            if (!_LoopPoints.empty())
            {
                const auto _RoleIter = _WireRoles.find(_Wire.Id);
                _pTopology->Loops.emplace_back(_MakeProjectedLoop(
                    _Wire.Id,
                    "loop " + std::to_string(_Wire.Id),
                    _LoopPoints,
                    _Bounds,
                    _RoleIter == _WireRoles.end() ? std::string("cut") : _RoleIter->second));
            }
        }

        for (const auto& _Edge : Model_.Edges)
        {
            auto _Points = _SampleEdgePoints(Model_, _Edge);
            if (!_Points.empty())
            {
                _pTopology->Edges.emplace_back(_MakeProjectedEdge(
                    _Edge.Id,
                    "edge " + std::to_string(_Edge.Id),
                    _Points,
                    _Bounds));
            }
        }

        _pTopology->Metadata["sourceDisplayName"] = strDisplayName_;
        _pTopology->Metadata["faceCount"] = static_cast<unsigned long long>(_pTopology->Faces.size());
        _pTopology->Metadata["loopCount"] = static_cast<unsigned long long>(_pTopology->Loops.size());
        _pTopology->Metadata["edgeCount"] = static_cast<unsigned long long>(_pTopology->Edges.size());
        return _pTopology;
    }

    std::string _FindImportedResourceID(IN const iCAX::Resource::CResourceImportResult& Result_, IN const std::string& strRole_)
    {
        auto _Ite = std::find_if(Result_.Items.begin(), Result_.Items.end(), [&strRole_](IN const iCAX::Resource::CResourceImportItem& Item_) {
            return Item_.Role == strRole_;
        });
        return _Ite == Result_.Items.end() ? std::string() : _Ite->ResourceID;
    }

    std::string _ToLowerASCII(IN std::string strText_)
    {
        std::transform(strText_.begin(), strText_.end(), strText_.begin(), [](unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return strText_;
    }

    bool _IsSupportedWorkpieceModelPath(IN const std::string& strSourcePath_)
    {
        const auto _Extension = _ToLowerASCII(std::filesystem::path(strSourcePath_).extension().string());
        return _Extension == ".step" || _Extension == ".stp" || _Extension == ".igs" || _Extension == ".iges";
    }

    void _RequireImportedResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strResourceID_,
        IN const std::string& strFieldName_)
    {
        if (strResourceID_.empty())
        {
            throw std::runtime_error("Resource import returned empty resource id: " + strFieldName_);
        }
        if (Scene_.Resources().GetVersion(strResourceID_) == 0)
        {
            throw std::runtime_error("Resource import returned resource id that was not saved: " + strFieldName_);
        }
    }

    SImportedCadResources _ImportCadModel(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Product::IProductContext& Product_,
        IN const std::string& strSourcePath_,
        IN double dTolerance_)
    {
        auto& _Resources = Scene_.Resources();
        SImportedCadResources _Imported;
        std::shared_ptr<iCAX::GeometryData::BRepModel> _pBRep;
        std::string _ImportMode;
        std::string _ContentHash;

        std::optional<iCAX::Data::Variant> _SectionDefinitions;
        const auto& _Capabilities = Product_.GetDefinition().Capabilities;
        if (const auto _Tube = _Capabilities.find("tube");
            _Tube != _Capabilities.end()
            && _Tube->second.Is<iCAX::Data::ObjectMap>())
        {
            const auto _TubeObject = _Tube->second.To<iCAX::Data::ObjectMap>();
            if (const auto _Definitions = _TubeObject.find("sectionTypeDefinitions");
                _Definitions != _TubeObject.end())
            {
                _SectionDefinitions = _Definitions->second;
            }
        }

        if (_SectionDefinitions)
        {
            const auto _Read = iCAX::OpenCascade::ReadBRepFile(
                strSourcePath_,
                dTolerance_);
            if (!_Read.bOK)
            {
                std::ostringstream _Message;
                _Message << "CAD 文件解析失败";
                for (const auto& _Diagnostic : _Read.Diagnostics)
                {
                    _Message << ": " << _Diagnostic;
                }
                throw std::runtime_error(_Message.str());
            }

            const auto _pRecognition = Scene_.Services().Resolve<
                iCAX::ExtrusionRecognition::IExtrusionRecognitionService>();
            const auto _LoadedDefinitions = _pRecognition->DecodeDefinitions(
                *_SectionDefinitions);
            if (!_LoadedDefinitions.bValid)
            {
                std::ostringstream _Message;
                _Message << "管型定义无效";
                for (const auto& _Diagnostic : _LoadedDefinitions.Diagnostics)
                {
                    _Message << ": " << _Diagnostic;
                }
                throw std::runtime_error(_Message.str());
            }

            iCAX::ExtrusionRecognition::SRecognitionOptions _Options;
            _Options.TargetAxis = { 0.0, 1.0, 0.0 };
            _Options.dLinearTolerance = dTolerance_;
            const auto _Recognition = _pRecognition->Recognize(
                _Read.Geometry,
                _LoadedDefinitions.Definitions,
                _Options);
            if (!_Recognition.IsOK())
            {
                std::ostringstream _Message;
                _Message << "图纸并非可识别的拉伸体";
                for (const auto& _Diagnostic : _Recognition.Diagnostics)
                {
                    _Message << ": " << _Diagnostic;
                }
                throw std::runtime_error(_Message.str());
            }

            const auto _GeometryResourceID = _Resources.AllocateResourceURL();
            const auto _GeometryVersion = _NextResourceVersion(
                _Resources,
                _GeometryResourceID);
            _pBRep = std::make_shared<iCAX::GeometryData::BRepModel>(
                _Recognition.NormalizedGeometry);
            auto _GeometryInfo = _MakeResourceInfo(
                _GeometryResourceID,
                _Read.DisplayName + " normalized geometry",
                iCAX::GeometryData::BRepModel::kResourceTypeName,
                iCAX::Resource::EResourcePersistenceMode::Embedded,
                _GeometryVersion);
            _GeometryInfo.Source = strSourcePath_;
            _GeometryInfo.ContentHash = _Read.ContentHash;
            _GeometryInfo.Metadata["importer"] = "occ.memory-brep";
            _GeometryInfo.Metadata["normalization"] = "extrusion-to-positive-y";
            _GeometryInfo.Metadata["sectionTypeId"] = _Recognition.SectionTypeID;
            _GeometryInfo.Metadata["length"] = std::to_string(_Recognition.dLength);
            _Resources.Set<iCAX::GeometryData::BRepModel>(
                _GeometryResourceID,
                _pBRep,
                _GeometryInfo);

            // 兼容现有通用 CAM 调用字段；三个字段指向同一个规范化几何资源。
            _Imported.ModelResourceID = _GeometryResourceID;
            _Imported.BRepResourceID = _GeometryResourceID;
            _Imported.SectionTypeID = _Recognition.SectionTypeID;
            _Imported.SectionParameters = _Recognition.SectionParameters;
            _Imported.dLength = _Recognition.dLength;
            _ImportMode = "extrusion-recognition";
            _ContentHash = _Read.ContentHash;
        }
        else
        {
            iCAX::Resource::CResourceImportRequest _Request;
            _Request.SourcePath = strSourcePath_;
            _Request.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
            _Request.Options["tolerance"] = std::to_string(dTolerance_);

            iCAX::Resource::CResourceImportResult _Result;
            _pBRep = _Resources.Import<iCAX::GeometryData::BRepModel>(
                _Request,
                &_Result);
            if (!_Result.IsOK())
            {
                throw std::runtime_error(
                    _Result.Error.empty()
                        ? "CAD resource import failed"
                        : _Result.Error);
            }
            _Imported.ModelResourceID = _FindImportedResourceID(_Result, "source");
            if (_Imported.ModelResourceID.empty())
            {
                _Imported.ModelResourceID = _Result.PrimaryResourceID;
            }
            _Imported.BRepResourceID = _FindImportedResourceID(
                _Result,
                "geometry.brep");
            _Imported.TubeNeutralGeometryResourceID = _FindImportedResourceID(
                _Result,
                "tube.neutral_geometry");
            _ImportMode = _Result.Metadata.contains("importer")
                ? _Result.Metadata.at("importer")
                : std::string("resource-import");
            if (const auto _Hash = _Result.Metadata.find("contentHash");
                _Hash != _Result.Metadata.end())
            {
                _ContentHash = _Hash->second;
            }
        }

        _RequireImportedResource(Scene_, _Imported.ModelResourceID, "modelResourceId");
        _RequireImportedResource(Scene_, _Imported.BRepResourceID, "brepResourceId");
        if (!_pBRep)
        {
            throw std::runtime_error("Resource import returned BRep resource with unexpected runtime type");
        }

        _Imported.TopologyResourceID =
            iCAX::CAM::MakeTopologyResourceID(
                _Resources,
                _Imported.ModelResourceID);
        _Imported.nTopologyVersion = _NextResourceVersion(_Resources, _Imported.TopologyResourceID);
        auto _pTopology = _MakeTopologyResourceFromBRep(*_pBRep, _GetDisplayNameFromPath(strSourcePath_), _Imported.nTopologyVersion);
        _pTopology->Metadata["importMode"] = _ImportMode;
        _pTopology->Metadata["sourcePath"] = strSourcePath_;
        _pTopology->Metadata["sourceResourceId"] = _Imported.ModelResourceID;
        _pTopology->Metadata["brepResourceId"] = _Imported.BRepResourceID;
        if (!_ContentHash.empty())
        {
            _pTopology->Metadata["contentHash"] = _ContentHash;
        }
        if (!_Imported.SectionTypeID.empty())
        {
            _pTopology->Metadata["sectionTypeId"] = _Imported.SectionTypeID;
            _pTopology->Metadata["length"] = std::to_string(_Imported.dLength);
        }

        auto _TopologyInfo = _MakeResourceInfo(
            _Imported.TopologyResourceID,
            _GetDisplayNameFromPath(strSourcePath_) + " topology",
            "topology",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Imported.nTopologyVersion);
        _TopologyInfo.Source = strSourcePath_;
        _TopologyInfo.Metadata["sourceResourceId"] = _Imported.ModelResourceID;
        _TopologyInfo.Metadata["brepResourceId"] = _Imported.BRepResourceID;
        _Resources.Set<iCAX::CAM::CTopologyResource>(_Imported.TopologyResourceID, _pTopology, _TopologyInfo);

        _RequireImportedResource(Scene_, _Imported.TopologyResourceID, "topologyResourceId");
        return _Imported;
    }

}
