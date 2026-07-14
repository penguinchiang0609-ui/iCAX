#include "pch.h"
#include "ToolpathTargetImplement.h"

#include "ToolpathResourceKeys.h"

#include <algorithm>
#include <unordered_set>

namespace iCAX::CAM::Commands::Internal
{
namespace
{
    constexpr const char* kProgramChildKindBlock = "block";
    constexpr const char* kProgramChildKindPath = "path";
}

    VariantArray _NormalizeNumericArray(
        IN const ObjectMap& Object_,
        IN const std::string& strFieldName_,
        IN size_t nExpectedSize_)
    {
        auto _Iter = Object_.find(strFieldName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<VariantArray>())
        {
            throw std::invalid_argument("Cam pose sample field must be an array: " + strFieldName_);
        }

        const auto _Input = _Iter->second.To<VariantArray>();
        if (_Input.size() != nExpectedSize_)
        {
            throw std::invalid_argument("Cam pose sample field has invalid size: " + strFieldName_);
        }

        VariantArray _Result;
        _Result.reserve(nExpectedSize_);
        for (size_t _Index = 0; _Index < _Input.size(); ++_Index)
        {
            _Result.emplace_back(_ToDouble(_Input[_Index], strFieldName_ + "[" + std::to_string(_Index) + "]"));
        }
        return _Result;
    }

    iCAX::GeometryData::Direction3 _ReadDirection3(
        IN const ObjectMap& Object_,
        IN const std::string& strFieldName_)
    {
        const auto _Values = _NormalizeNumericArray(Object_, strFieldName_, 3);
        iCAX::GeometryData::Direction3 _Direction;
        _Direction.X = _Values[0].To<double>();
        _Direction.Y = _Values[1].To<double>();
        _Direction.Z = _Values[2].To<double>();
        return _Direction;
    }

    iCAX::CAM::SPoseSample _NormalizePoseSample(IN const Variant& Sample_, IN size_t nSampleIndex_)
    {
        if (!Sample_.Is<ObjectMap>())
        {
            throw std::invalid_argument("Cam pose sample must be an object at index: " + std::to_string(nSampleIndex_));
        }

        const auto _Input = Sample_.To<ObjectMap>();
        iCAX::CAM::SPoseSample _Sample;
        _Sample.nSegmentIndex = _GetObjectUInt64(_Input, "segmentIndex");
        _Sample.dSegmentU = _GetObjectDouble(_Input, "segmentU");
        _Sample.BeamDirection = _ReadDirection3(_Input, "beamDirection");
        return _Sample;
    }

    std::vector<iCAX::CAM::SPoseSample> _NormalizePoseSamples(IN const VariantArray& Samples_)
    {
        std::vector<iCAX::CAM::SPoseSample> _Result;
        _Result.reserve(Samples_.size());
        for (size_t _Index = 0; _Index < Samples_.size(); ++_Index)
        {
            _Result.emplace_back(_NormalizePoseSample(Samples_[_Index], _Index));
        }
        return _Result;
    }

    ObjectMap _MakeProgramChildReference(IN const std::string& strKind_, IN const iCAX::Data::uuid& EntityID_)
    {
        ObjectMap _Child;
        _Child["kind"] = strKind_;
        _Child["entityId"] = _UuidToString(EntityID_);
        return _Child;
    }

    std::optional<std::pair<std::string, iCAX::Data::uuid>> _TryReadProgramChildReference(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }

        const auto _Object = Value_.To<ObjectMap>();
        const auto _KindIter = _Object.find("kind");
        const auto _IDIter = _Object.find("entityId");
        if (_KindIter == _Object.end() || _IDIter == _Object.end()
            || !_KindIter->second.Is<std::string>() || !_IDIter->second.Is<std::string>())
        {
            return std::nullopt;
        }

        const auto _ParsedID = iCAX::Data::uuid::from_string(_IDIter->second.To<std::string>());
        if (!_ParsedID.has_value())
        {
            return std::nullopt;
        }
        return std::make_pair(_KindIter->second.To<std::string>(), *_ParsedID);
    }

    ObjectMap _MakeProgramNodeCommonPayload(IN const std::shared_ptr<iCAX::CAM::CProgramNodeComponent>& pNode_)
    {
        ObjectMap _Node;
        _Node["name"] = pNode_ ? pNode_->GetName() : std::string();
        _Node["preInstructions"] = pNode_ ? pNode_->GetPreInstructions() : VariantArray{};
        _Node["postInstructions"] = pNode_ ? pNode_->GetPostInstructions() : VariantArray{};
        return _Node;
    }

    ObjectMap _MakeCuttingLayerPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CCuttingLayerComponent>& pLayer_)
    {
        ObjectMap _Layer;
        _Layer["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Layer["id"] = _Layer["entityId"];
        _Layer["name"] = pLayer_ ? pLayer_->GetName() : std::string();
        _Layer["cuttingProcessId"] = pLayer_ ? pLayer_->GetCuttingProcessID() : std::string();
        _Layer["cuttingProcessName"] = pLayer_ ? pLayer_->GetCuttingProcessName() : std::string();
        _Layer["enabled"] = pLayer_ ? pLayer_->GetEnabled() : false;
        _Layer["outputOrder"] = pLayer_ ? pLayer_->GetOutputOrder() : 0ull;
        return _Layer;
    }

    ObjectMap _MakeVisibleLayerPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CVisibleLayerComponent>& pLayer_)
    {
        ObjectMap _Layer;
        _Layer["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Layer["id"] = _Layer["entityId"];
        _Layer["name"] = pLayer_ ? pLayer_->GetName() : std::string();
        _Layer["color"] = pLayer_ ? pLayer_->GetColor() : std::string();
        _Layer["visible"] = pLayer_ ? pLayer_->GetVisible() : false;
        _Layer["locked"] = pLayer_ ? pLayer_->GetLocked() : false;
        _Layer["order"] = pLayer_ ? pLayer_->GetOrder() : 0ull;
        return _Layer;
    }

    VariantArray _MakeCuttingLayerArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Layers;
        auto _Items = _CollectEntitiesWithComponent<iCAX::CAM::CCuttingLayerComponent>(Repository_);
        std::sort(_Items.begin(), _Items.end(), [](const auto& Lhs_, const auto& Rhs_)
        {
            return Lhs_.second->GetOutputOrder() < Rhs_.second->GetOutputOrder();
        });
        for (const auto& [_pEntity, _pLayer] : _Items)
        {
            _Layers.emplace_back(_MakeCuttingLayerPayload(_pEntity, _pLayer));
        }
        return _Layers;
    }

    VariantArray _MakeVisibleLayerArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Layers;
        auto _Items = _CollectEntitiesWithComponent<iCAX::CAM::CVisibleLayerComponent>(Repository_);
        std::sort(_Items.begin(), _Items.end(), [](const auto& Lhs_, const auto& Rhs_)
        {
            return Lhs_.second->GetOrder() < Rhs_.second->GetOrder();
        });
        for (const auto& [_pEntity, _pLayer] : _Items)
        {
            _Layers.emplace_back(_MakeVisibleLayerPayload(_pEntity, _pLayer));
        }
        return _Layers;
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CCuttingLayerComponent>> _CreateCuttingLayerEntity(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strName_,
        IN const std::string& strCuttingProcessID_,
        IN const std::string& strCuttingProcessName_,
        IN unsigned long long nOutputOrder_)
    {
        auto [_pEntity, _pLayer] = _CreateEntityWithComponent<iCAX::CAM::CCuttingLayerComponent>(Repository_);
        _SetStringProperty(_pLayer, iCAX::CAM::CCuttingLayerComponent::PropertyName_Name, strName_);
        _SetStringProperty(_pLayer, iCAX::CAM::CCuttingLayerComponent::PropertyName_CuttingProcessID, strCuttingProcessID_);
        _SetStringProperty(_pLayer, iCAX::CAM::CCuttingLayerComponent::PropertyName_CuttingProcessName, strCuttingProcessName_);
        _SetBoolProperty(_pLayer, iCAX::CAM::CCuttingLayerComponent::PropertyName_Enabled, true);
        _SetUInt64Property(_pLayer, iCAX::CAM::CCuttingLayerComponent::PropertyName_OutputOrder, nOutputOrder_);
        return { _pEntity, _pLayer };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CVisibleLayerComponent>> _CreateVisibleLayerEntity(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strName_,
        IN const std::string& strColor_,
        IN unsigned long long nOrder_)
    {
        auto [_pEntity, _pLayer] = _CreateEntityWithComponent<iCAX::CAM::CVisibleLayerComponent>(Repository_);
        _SetStringProperty(_pLayer, iCAX::CAM::CVisibleLayerComponent::PropertyName_Name, strName_);
        _SetStringProperty(_pLayer, iCAX::CAM::CVisibleLayerComponent::PropertyName_Color, strColor_);
        _SetBoolProperty(_pLayer, iCAX::CAM::CVisibleLayerComponent::PropertyName_Visible, true);
        _SetBoolProperty(_pLayer, iCAX::CAM::CVisibleLayerComponent::PropertyName_Locked, false);
        _SetUInt64Property(_pLayer, iCAX::CAM::CVisibleLayerComponent::PropertyName_Order, nOrder_);
        return { _pEntity, _pLayer };
    }

    iCAX::Data::uuid _EnsureDefaultCuttingLayer(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(Repository_);
        if (!_pRoot->GetDefaultCuttingLayerID().is_nil())
        {
            auto _pEntity = Repository_.GetEntity(_pRoot->GetDefaultCuttingLayerID());
            if (_GetComponent<iCAX::CAM::CCuttingLayerComponent>(_pEntity))
            {
                return _pRoot->GetDefaultCuttingLayerID();
            }
        }

        auto _Existing = _CollectEntitiesWithComponent<iCAX::CAM::CCuttingLayerComponent>(Repository_);
        if (!_Existing.empty() && _Existing.front().first)
        {
            _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_DefaultCuttingLayerID, _Existing.front().first->GetID());
            return _Existing.front().first->GetID();
        }

        auto [_pLayerEntity, _pLayer] = _CreateCuttingLayerEntity(Repository_, "Default Cutting", "default", "Default Cutting Process", 0ull);
        _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_DefaultCuttingLayerID, _pLayerEntity->GetID());
        return _pLayerEntity->GetID();
    }

    iCAX::Data::uuid _EnsureDefaultVisibleLayer(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(Repository_);
        if (!_pRoot->GetDefaultVisibleLayerID().is_nil())
        {
            auto _pEntity = Repository_.GetEntity(_pRoot->GetDefaultVisibleLayerID());
            if (_GetComponent<iCAX::CAM::CVisibleLayerComponent>(_pEntity))
            {
                return _pRoot->GetDefaultVisibleLayerID();
            }
        }

        auto _Existing = _CollectEntitiesWithComponent<iCAX::CAM::CVisibleLayerComponent>(Repository_);
        if (!_Existing.empty() && _Existing.front().first)
        {
            _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_DefaultVisibleLayerID, _Existing.front().first->GetID());
            return _Existing.front().first->GetID();
        }

        auto [_pLayerEntity, _pLayer] = _CreateVisibleLayerEntity(Repository_, "Default Visible", "#2F80ED", 0ull);
        _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_DefaultVisibleLayerID, _pLayerEntity->GetID());
        return _pLayerEntity->GetID();
    }

    std::pair<iCAX::Data::uuid, iCAX::Data::uuid> _EnsureDefaultLayers(IN iCAX::Database::IRepository& Repository_)
    {
        return { _EnsureDefaultCuttingLayer(Repository_), _EnsureDefaultVisibleLayer(Repository_) };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CBlockComponent>> _GetProgramRootBlock(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetComponent<iCAX::CAM::CRootComponent>(Repository_);
        if (!_pRoot || _pRoot->GetProgramRootBlockID().is_nil())
        {
            return {};
        }

        auto _pEntity = Repository_.GetEntity(_pRoot->GetProgramRootBlockID());
        auto _pBlock = _GetComponent<iCAX::CAM::CBlockComponent>(_pEntity);
        if (!_pEntity || !_pBlock)
        {
            return {};
        }
        return { _pEntity, _pBlock };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CBlockComponent>> _CreateBlockEntity(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strName_)
    {
        auto [_pEntity, _pBlock] = _CreateEntityWithComponent<iCAX::CAM::CBlockComponent>(Repository_);
        _SetStringProperty(_pBlock, iCAX::CAM::CProgramNodeComponent::PropertyName_Name, strName_);
        _SetVariantArrayProperty(_pBlock, iCAX::CAM::CBlockComponent::PropertyName_Children, VariantArray{});
        return { _pEntity, _pBlock };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CBlockComponent>> _EnsureProgramRootBlock(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pExistingEntity, _pExistingBlock] = _GetProgramRootBlock(Repository_);
        if (_pExistingEntity && _pExistingBlock)
        {
            return { _pExistingEntity, _pExistingBlock };
        }

        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(Repository_);
        auto [_pBlockEntity, _pBlock] = _CreateBlockEntity(Repository_, "Program");
        _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ProgramRootBlockID, _pBlockEntity->GetID());
        return { _pBlockEntity, _pBlock };
    }

    bool _BlockContainsChild(
        IN const std::shared_ptr<iCAX::CAM::CBlockComponent>& pBlock_,
        IN const std::string& strKind_,
        IN const iCAX::Data::uuid& EntityID_)
    {
        if (!pBlock_)
        {
            return false;
        }

        for (const auto& _Child : pBlock_->GetChildren())
        {
            auto _Reference = _TryReadProgramChildReference(_Child);
            if (!_Reference.has_value())
            {
                continue;
            }
            if (_Reference->first == strKind_ && _Reference->second == EntityID_)
            {
                return true;
            }
        }
        return false;
    }

    void _AppendChildToBlock(
        IN const std::shared_ptr<iCAX::CAM::CBlockComponent>& pBlock_,
        IN const std::string& strKind_,
        IN const iCAX::Data::uuid& EntityID_)
    {
        if (!pBlock_)
        {
            throw std::invalid_argument("Cam program block is null");
        }
        if (_BlockContainsChild(pBlock_, strKind_, EntityID_))
        {
            return;
        }

        auto _Children = pBlock_->GetChildren();
        _Children.emplace_back(_MakeProgramChildReference(strKind_, EntityID_));
        _SetVariantArrayProperty(pBlock_, iCAX::CAM::CBlockComponent::PropertyName_Children, _Children);
    }

    void _AppendPathToProgramRoot(IN iCAX::Database::IRepository& Repository_, IN const iCAX::Data::uuid& PathEntityID_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _EnsureProgramRootBlock(Repository_);
        _AppendChildToBlock(_pRootBlock, kProgramChildKindPath, PathEntityID_);
    }

    void _ClearProgramRootBlockChildren(IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _EnsureProgramRootBlock(Repository_);
        _SetVariantArrayProperty(_pRootBlock, iCAX::CAM::CBlockComponent::PropertyName_Children, VariantArray{});
    }

    void _DeleteNonRootProgramBlocks(IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _GetProgramRootBlock(Repository_);
        const auto _RootBlockID = _pRootBlockEntity ? _pRootBlockEntity->GetID() : iCAX::Data::uuid();
        for (const auto& [_pEntity, _pBlock] : _CollectEntitiesWithComponent<iCAX::CAM::CBlockComponent>(Repository_))
        {
            if (!_pEntity || _pEntity->GetID() == _RootBlockID)
            {
                continue;
            }

            std::string _strError;
            if (!Repository_.DeleteEntity(_pEntity->GetID(), _strError))
            {
                throw std::runtime_error(_strError.empty() ? "Cam delete program block failed" : _strError);
            }
        }
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CWorkpieceComponent>> _GetActiveWorkpiece(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetComponent<iCAX::CAM::CRootComponent>(Repository_);
        if (!_pRoot || _pRoot->GetActiveWorkpieceID().is_nil())
        {
            return {};
        }

        auto _pEntity = Repository_.GetEntity(_pRoot->GetActiveWorkpieceID());
        auto _pWorkpiece = _GetComponent<iCAX::CAM::CWorkpieceComponent>(_pEntity);
        if (!_pEntity || !_pWorkpiece)
        {
            return {};
        }
        return { _pEntity, _pWorkpiece };
    }

    std::shared_ptr<iCAX::CAM::CTopologyResource> _GetTopologyResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_)
    {
        if (!pWorkpiece_ || pWorkpiece_->GetTopologyResourceID().empty())
        {
            return nullptr;
        }
        return Scene_.Resources().Get<iCAX::CAM::CTopologyResource>(pWorkpiece_->GetTopologyResourceID());
    }

    const VariantArray& _GetTopologyArray(
        IN const iCAX::CAM::CTopologyResource& Topology_,
        IN const std::string& strKind_)
    {
        if (strKind_ == kTopologyKindFace)
        {
            return Topology_.Faces;
        }
        if (strKind_ == kTopologyKindLoop)
        {
            return Topology_.Loops;
        }
        if (strKind_ == kTopologyKindEdge)
        {
            return Topology_.Edges;
        }
        throw std::invalid_argument("Unsupported topology kind: " + strKind_);
    }

    std::optional<ObjectMap> _FindTopologyObject(
        IN const iCAX::CAM::CTopologyResource& Topology_,
        IN const std::string& strKind_,
        IN unsigned long long nID_)
    {
        for (const auto& _Item : _GetTopologyArray(Topology_, strKind_))
        {
            if (!_Item.Is<ObjectMap>())
            {
                continue;
            }

            auto _Object = _Item.To<ObjectMap>();
            if (_GetObjectUInt64(_Object, "id") == nID_)
            {
                return _Object;
            }
        }
        return std::nullopt;
    }

    std::string _ResolveTopologyLabel(
        IN const iCAX::CAM::CTopologyResource& Topology_,
        IN const std::string& strKind_,
        IN unsigned long long nID_)
    {
        auto _TopologyObject = _FindTopologyObject(Topology_, strKind_, nID_);
        if (!_TopologyObject.has_value())
        {
            return {};
        }
        return _GetObjectString(*_TopologyObject, "label");
    }

    bool _HasPathForTopology(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& WorkpieceEntityID_,
        IN const std::string& strKind_,
        IN unsigned long long nTopologyID_)
    {
        for (const auto& [_pEntity, _pPath] : _CollectEntitiesWithComponent<iCAX::CAM::CPathComponent>(Repository_))
        {
            if (_pPath->GetWorkpieceEntityID() == WorkpieceEntityID_
                && _pPath->GetTopologyKind() == strKind_
                && _pPath->GetTopologyID() == nTopologyID_)
            {
                return true;
            }
        }
        return false;
    }

    std::string _ResolveOperation(IN const std::string& strKind_, IN const std::string& strRole_)
    {
        if (strKind_ == kTopologyKindLoop)
        {
            return strRole_ == "outer" || strRole_ == "trim" ? "TrimLoop" : "CutLoop";
        }
        if (strKind_ == kTopologyKindEdge)
        {
            return "TrimEdge";
        }
        if (strKind_ == kTopologyKindFace)
        {
            return "CutFace";
        }
        return "TopologyOperation";
    }

    iCAX::Resource::CResourceInfo _MakeResourceInfo(
        IN const std::string& strSource_,
        IN const std::string& strName_,
        IN const std::string& strKind_,
        IN iCAX::Resource::EResourcePersistenceMode Persistence_,
        IN uint64_t nVersion_);

    std::string _CreatePoseFieldResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& PathEntityID_,
        IN const std::string& strPathName_)
    {
        const auto _PoseFieldResourceID = iCAX::CAM::MakePoseFieldResourceID(PathEntityID_);
        auto _pPoseField = std::make_shared<iCAX::CAM::CPoseFieldResource>();
        _pPoseField->nVersion = 1;

        auto _PoseFieldInfo = _MakeResourceInfo(
            _PoseFieldResourceID,
            strPathName_ + " pose field",
            "pose-field",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _pPoseField->nVersion);
        Scene_.Resources().Set<iCAX::CAM::CPoseFieldResource>(_PoseFieldResourceID, _pPoseField, _PoseFieldInfo);
        return _PoseFieldResourceID;
    }

    std::shared_ptr<iCAX::CAM::CPathComponent> _CreatePathEntity(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& WorkpieceEntityID_,
        IN const std::string& strKind_,
        IN unsigned long long nTopologyID_,
        IN const ObjectMap& TopologyObject_,
        IN const std::string& strSource_)
    {
        const auto _Label = _GetObjectString(TopologyObject_, "label", strKind_ + " " + std::to_string(nTopologyID_));
        const auto _Role = _GetObjectString(TopologyObject_, "role");
        auto& _Repository = Scene_.Database();
        const auto [_DefaultCuttingLayerID, _DefaultVisibleLayerID] = _EnsureDefaultLayers(_Repository);
        auto [_pEntity, _pPath] = _CreateEntityWithComponent<iCAX::CAM::CPathComponent>(_Repository);
        const auto _CurveResourceID = iCAX::CAM::MakePathCurveResourceID(_pEntity->GetID());

        auto _pCurve = std::make_shared<iCAX::CAM::CPathCurveResource>();
        _pCurve->nVersion = 1;
        _pCurve->TopologyKind = strKind_;
        _pCurve->TopologyID = nTopologyID_;
        _pCurve->SourceTopology = TopologyObject_;

        auto _CurveInfo = _MakeResourceInfo(
            _CurveResourceID,
            _Label + " curve",
            "path-curve",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _pCurve->nVersion);
        Scene_.Resources().Set<iCAX::CAM::CPathCurveResource>(_CurveResourceID, _pCurve, _CurveInfo);
        const auto _PoseFieldResourceID = _CreatePoseFieldResource(Scene_, _pEntity->GetID(), _Label);

        _SetUuidProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_WorkpieceEntityID, WorkpieceEntityID_);
        _SetUuidProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_CuttingLayerID, _DefaultCuttingLayerID);
        _SetUuidProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_VisibleLayerID, _DefaultVisibleLayerID);
        _SetStringProperty(_pPath, iCAX::CAM::CProgramNodeComponent::PropertyName_Name, _Label);
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_PathKind, "Cut");
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_TopologyKind, strKind_);
        _SetUInt64Property(_pPath, iCAX::CAM::CPathComponent::PropertyName_TopologyID, nTopologyID_);
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_Source, strSource_);
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_Operation, _ResolveOperation(strKind_, _Role));
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_Role, _Role);
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_CurveResourceID, _CurveResourceID);
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_PoseFieldResourceID, _PoseFieldResourceID);
        _AppendPathToProgramRoot(_Repository, _pEntity->GetID());
        return _pPath;
    }

    ObjectMap _MakePathPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CPathComponent>& pPath_,
        IN unsigned long long nOrderIndex_ = 0ull)
    {
        ObjectMap _Path;
        auto _Common = _MakeProgramNodeCommonPayload(std::dynamic_pointer_cast<iCAX::CAM::CProgramNodeComponent>(pPath_));
        for (const auto& [_Name, _Value] : _Common)
        {
            _Path[_Name] = _Value;
        }

        const auto _EntityID = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Path["nodeKind"] = std::string(kProgramChildKindPath);
        _Path["entityId"] = _EntityID;
        _Path["id"] = _EntityID;
        _Path["orderIndex"] = nOrderIndex_;
        _Path["workpieceEntityId"] = pPath_ ? _UuidToString(pPath_->GetWorkpieceEntityID()) : std::string();
        _Path["cuttingLayerId"] = pPath_ ? _UuidToString(pPath_->GetCuttingLayerID()) : std::string();
        _Path["visibleLayerId"] = pPath_ ? _UuidToString(pPath_->GetVisibleLayerID()) : std::string();
        _Path["pathKind"] = pPath_ ? pPath_->GetPathKind() : std::string();
        _Path["topologyKind"] = pPath_ ? pPath_->GetTopologyKind() : std::string();
        _Path["topologyId"] = pPath_ ? pPath_->GetTopologyID() : 0ull;
        _Path["label"] = pPath_ ? pPath_->GetName() : std::string();
        _Path["source"] = pPath_ ? pPath_->GetSource() : std::string();
        _Path["operation"] = pPath_ ? pPath_->GetOperation() : std::string();
        _Path["role"] = pPath_ ? pPath_->GetRole() : std::string();
        _Path["curveResourceId"] = pPath_ ? pPath_->GetCurveResourceID() : std::string();
        _Path["poseFieldResourceId"] = pPath_ ? pPath_->GetPoseFieldResourceID() : std::string();
        return _Path;
    }

    ObjectMap _MakeProgramBlockPayload(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CBlockComponent>& pBlock_,
        IN OUT std::unordered_set<iCAX::Data::uuid>& VisitedBlocks_)
    {
        ObjectMap _Block = _MakeProgramNodeCommonPayload(std::dynamic_pointer_cast<iCAX::CAM::CProgramNodeComponent>(pBlock_));
        _Block["nodeKind"] = std::string(kProgramChildKindBlock);
        _Block["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();

        VariantArray _Children;
        if (!pEntity_ || !pBlock_ || !VisitedBlocks_.insert(pEntity_->GetID()).second)
        {
            _Block["children"] = _Children;
            return _Block;
        }

        unsigned long long _OrderIndex = 0;
        for (const auto& _Child : pBlock_->GetChildren())
        {
            auto _Reference = _TryReadProgramChildReference(_Child);
            if (!_Reference.has_value())
            {
                continue;
            }

            auto _pChildEntity = Repository_.GetEntity(_Reference->second);
            if (_Reference->first == kProgramChildKindPath)
            {
                auto _pPath = _GetComponent<iCAX::CAM::CPathComponent>(_pChildEntity);
                if (_pChildEntity && _pPath)
                {
                    _Children.emplace_back(_MakePathPayload(_pChildEntity, _pPath, ++_OrderIndex));
                }
                continue;
            }

            if (_Reference->first == kProgramChildKindBlock)
            {
                auto _pChildBlock = _GetComponent<iCAX::CAM::CBlockComponent>(_pChildEntity);
                if (_pChildEntity && _pChildBlock)
                {
                    _Children.emplace_back(_MakeProgramBlockPayload(Repository_, _pChildEntity, _pChildBlock, VisitedBlocks_));
                }
            }
        }

        _Block["children"] = _Children;
        return _Block;
    }

    ObjectMap _MakeProgramPayload(IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _GetProgramRootBlock(Repository_);
        if (!_pRootBlockEntity || !_pRootBlock)
        {
            return ObjectMap{};
        }

        std::unordered_set<iCAX::Data::uuid> _VisitedBlocks;
        return _MakeProgramBlockPayload(Repository_, _pRootBlockEntity, _pRootBlock, _VisitedBlocks);
    }

    void _AppendPathsFromBlock(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::CAM::CBlockComponent>& pBlock_,
        IN OUT VariantArray& Result_,
        IN OUT std::unordered_set<iCAX::Data::uuid>& VisitedBlocks_,
        IN OUT std::unordered_set<iCAX::Data::uuid>& VisitedPaths_,
        IN OUT unsigned long long& nOrderIndex_)
    {
        if (!pBlock_)
        {
            return;
        }

        auto _pBlockEntity = pBlock_->GetEntity();
        if (!_pBlockEntity || !VisitedBlocks_.insert(_pBlockEntity->GetID()).second)
        {
            return;
        }

        for (const auto& _Child : pBlock_->GetChildren())
        {
            auto _Reference = _TryReadProgramChildReference(_Child);
            if (!_Reference.has_value())
            {
                continue;
            }

            auto _pChildEntity = Repository_.GetEntity(_Reference->second);
            if (_Reference->first == kProgramChildKindPath)
            {
                auto _pPath = _GetComponent<iCAX::CAM::CPathComponent>(_pChildEntity);
                if (_pChildEntity && _pPath && VisitedPaths_.insert(_pChildEntity->GetID()).second)
                {
                    Result_.emplace_back(_MakePathPayload(_pChildEntity, _pPath, ++nOrderIndex_));
                }
                continue;
            }

            if (_Reference->first == kProgramChildKindBlock)
            {
                auto _pChildBlock = _GetComponent<iCAX::CAM::CBlockComponent>(_pChildEntity);
                _AppendPathsFromBlock(Repository_, _pChildBlock, Result_, VisitedBlocks_, VisitedPaths_, nOrderIndex_);
            }
        }
    }

    VariantArray _MakePathArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Result;
        auto [_pRootBlockEntity, _pRootBlock] = _GetProgramRootBlock(Repository_);
        if (!_pRootBlock)
        {
            return _Result;
        }

        std::unordered_set<iCAX::Data::uuid> _VisitedBlocks;
        std::unordered_set<iCAX::Data::uuid> _VisitedPaths;
        unsigned long long _OrderIndex = 0;
        _AppendPathsFromBlock(Repository_, _pRootBlock, _Result, _VisitedBlocks, _VisitedPaths, _OrderIndex);
        return _Result;
    }

}
