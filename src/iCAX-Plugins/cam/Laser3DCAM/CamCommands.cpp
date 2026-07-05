#include "pch.h"
#include "ToolpathComponents.h"
#include "ToolpathResourceKeys.h"
#include "ToolpathResources.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"
#include "Data/uuid.h"
#include "Data/VariantSerializer.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::PropertySet;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    constexpr const char* kTopologyKindFace = "face";
    constexpr const char* kTopologyKindLoop = "loop";
    constexpr const char* kTopologyKindEdge = "edge";
    constexpr const char* kProgramChildKindBlock = "block";
    constexpr const char* kProgramChildKindPath = "path";

    std::vector<uint8_t> _EncodePayload(IN const Variant& Payload_)
    {
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
        return std::vector<uint8_t>(_Text.begin(), _Text.end());
    }

    Variant _DecodePayload(IN const std::vector<uint8_t>& Payload_)
    {
        if (Payload_.empty())
        {
            return Variant(ObjectMap{});
        }

        const std::string _Text(Payload_.begin(), Payload_.end());
        return iCAX::Data::VariantSerializer::Deserialize(_Text);
    }

    ObjectMap _DecodeObjectPayload(IN const iCAX::Command::CCommandRequest& Request_)
    {
        auto _Payload = _DecodePayload(Request_.Payload);
        if (!_Payload.Is<ObjectMap>())
        {
            throw std::invalid_argument("Cam command payload must be an object");
        }
        return _Payload.To<ObjectMap>();
    }

    iCAX::Command::CCommandResponse _MakeResponse(IN const Variant& Payload_)
    {
        iCAX::Command::CCommandResponse _Response;
        _Response.nStatus = iCAX::Command::ECommandStatusCode::Ok;
        _Response.Payload = _EncodePayload(Payload_);
        return _Response;
    }

    iCAX::Project::ISceneContext& _RequireSceneContext(IN iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pSceneContext_)
        {
            throw std::invalid_argument("Cam command must be sent to a project mailbox");
        }
        return *pSceneContext_;
    }

    unsigned long long _ToUInt64(IN const Variant& Value_, IN const std::string& strFieldName_)
    {
        if (Value_.Is<unsigned long long>())
        {
            return Value_.To<unsigned long long>();
        }
        if (Value_.Is<long long>())
        {
            const auto _Value = Value_.To<long long>();
            if (_Value < 0)
            {
                throw std::invalid_argument("Cam payload field cannot be negative: " + strFieldName_);
            }
            return static_cast<unsigned long long>(_Value);
        }
        if (Value_.Is<unsigned int>())
        {
            return static_cast<unsigned long long>(Value_.To<unsigned int>());
        }
        if (Value_.Is<int>())
        {
            const auto _Value = Value_.To<int>();
            if (_Value < 0)
            {
                throw std::invalid_argument("Cam payload field cannot be negative: " + strFieldName_);
            }
            return static_cast<unsigned long long>(_Value);
        }
        if (Value_.Is<double>())
        {
            const auto _Value = Value_.To<double>();
            if (_Value < 0.0)
            {
                throw std::invalid_argument("Cam payload field cannot be negative: " + strFieldName_);
            }
            return static_cast<unsigned long long>(_Value);
        }
        if (Value_.Is<std::string>())
        {
            return std::stoull(Value_.To<std::string>());
        }
        throw std::invalid_argument("Cam payload field must be numeric: " + strFieldName_);
    }

    double _ToDouble(IN const Variant& Value_, IN const std::string& strFieldName_)
    {
        if (Value_.Is<double>())
        {
            return Value_.To<double>();
        }
        if (Value_.Is<float>())
        {
            return static_cast<double>(Value_.To<float>());
        }
        if (Value_.Is<unsigned long long>())
        {
            return static_cast<double>(Value_.To<unsigned long long>());
        }
        if (Value_.Is<long long>())
        {
            return static_cast<double>(Value_.To<long long>());
        }
        if (Value_.Is<unsigned int>())
        {
            return static_cast<double>(Value_.To<unsigned int>());
        }
        if (Value_.Is<int>())
        {
            return static_cast<double>(Value_.To<int>());
        }
        if (Value_.Is<std::string>())
        {
            return std::stod(Value_.To<std::string>());
        }
        throw std::invalid_argument("Cam payload field must be numeric: " + strFieldName_);
    }

    std::string _GetOptionalString(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = std::string())
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument("Cam payload field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    unsigned long long _GetOptionalUInt64(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN unsigned long long nDefault_ = 0ull)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        return _ToUInt64(_Iter->second, strName_);
    }

    std::shared_ptr<iCAX::Database::CComponentBase> _GetOrCreateComponent(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strComponentClass_)
    {
        auto _pMetaEntity = Repository_.GetMetaEntity();
        if (!_pMetaEntity)
        {
            throw std::runtime_error("Cam command requires repository meta entity");
        }

        auto _pComponent = _pMetaEntity->GetComponent(strComponentClass_);
        if (_pComponent)
        {
            return _pComponent;
        }

        std::string _strError;
        if (!_pMetaEntity->AddComponent(strComponentClass_, _pComponent, _strError) || !_pComponent)
        {
            throw std::runtime_error(_strError.empty() ? "Cam component attach failed: " + strComponentClass_ : _strError);
        }
        return _pComponent;
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> _GetComponent(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pMetaEntity = Repository_.GetMetaEntity();
        if (!_pMetaEntity)
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<TComponent>(_pMetaEntity->GetComponent(TComponent::S_ClassName));
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> _GetOrCreateComponent(IN iCAX::Database::IRepository& Repository_)
    {
        return std::dynamic_pointer_cast<TComponent>(_GetOrCreateComponent(Repository_, TComponent::S_ClassName));
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<TComponent>(pEntity_->GetComponent(TComponent::S_ClassName));
    }

    template <typename TComponent>
    std::shared_ptr<TComponent> _AddComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            throw std::invalid_argument("Cam add component requires entity");
        }

        std::shared_ptr<iCAX::Database::CComponentBase> _pComponent;
        std::string _strError;
        if (!pEntity_->AddComponent(TComponent::S_ClassName, _pComponent, _strError) || !_pComponent)
        {
            throw std::runtime_error(_strError.empty() ? "Cam add component failed: " + std::string(TComponent::S_ClassName) : _strError);
        }
        return std::dynamic_pointer_cast<TComponent>(_pComponent);
    }

    template <typename TComponent>
    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>> _CreateEntityWithComponent(
        IN iCAX::Database::IRepository& Repository_)
    {
        std::shared_ptr<iCAX::Database::IEntity> _pEntity;
        std::string _strError;
        const auto _EntityID = iCAX::Data::GenerateNewUUID();
        if (!Repository_.CreateEntity(_EntityID, _pEntity, _strError) || !_pEntity)
        {
            throw std::runtime_error(_strError.empty() ? "Cam create entity failed" : _strError);
        }
        return { _pEntity, _AddComponent<TComponent>(_pEntity) };
    }

    bool _SetStringProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN const std::string& strValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(strValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetUInt64Property(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN unsigned long long nValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(nValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetBoolProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN bool bValue_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(bValue_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetUuidProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN const iCAX::Data::uuid& Value_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(Value_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    bool _SetVariantArrayProperty(
        IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
        IN const std::string& strPropertyName_,
        IN const VariantArray& Value_)
    {
        std::string _strError;
        if (!pComponent_->SetProperty(strPropertyName_, iCAX::Data::PropertyValue(Value_), _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam property set failed: " + strPropertyName_ : _strError);
        }
        return true;
    }

    std::string _GetObjectString(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = std::string())
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            return strDefault_;
        }
        return _Iter->second.To<std::string>();
    }

    unsigned long long _GetObjectUInt64(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN unsigned long long nDefault_ = 0ull)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        return _ToUInt64(_Iter->second, strName_);
    }

    double _GetObjectDouble(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN double nDefault_ = 0.0)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return nDefault_;
        }
        return _ToDouble(_Iter->second, strName_);
    }

    std::string _UuidToString(IN const iCAX::Data::uuid& ID_)
    {
        return ID_.is_nil() ? std::string() : iCAX::Data::to_string(ID_);
    }

    iCAX::Data::uuid _ParseRequiredUuid(IN const std::string& strValue_, IN const std::string& strFieldName_)
    {
        auto _Parsed = iCAX::Data::uuid::from_string(strValue_);
        if (!_Parsed.has_value() || _Parsed->is_nil())
        {
            throw std::invalid_argument("Cam payload field must be a valid uuid: " + strFieldName_);
        }
        return *_Parsed;
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

    ObjectMap _NormalizePoseSample(IN const Variant& Sample_, IN size_t nSampleIndex_)
    {
        if (!Sample_.Is<ObjectMap>())
        {
            throw std::invalid_argument("Cam pose sample must be an object at index: " + std::to_string(nSampleIndex_));
        }

        const auto _Input = Sample_.To<ObjectMap>();
        ObjectMap _Sample;
        _Sample["segmentIndex"] = _GetObjectUInt64(_Input, "segmentIndex");
        _Sample["segmentU"] = _GetObjectDouble(_Input, "segmentU");
        _Sample["beamDirection"] = _NormalizeNumericArray(_Input, "beamDirection", 3);
        return _Sample;
    }

    VariantArray _NormalizePoseSamples(IN const VariantArray& Samples_)
    {
        VariantArray _Result;
        _Result.reserve(Samples_.size());
        for (size_t _Index = 0; _Index < Samples_.size(); ++_Index)
        {
            _Result.emplace_back(_NormalizePoseSample(Samples_[_Index], _Index));
        }
        return _Result;
    }

    bool _IsTopologyResourceEmpty(IN const std::shared_ptr<iCAX::CAM::CCAMTopologyResource>& pTopology_) noexcept
    {
        return !pTopology_ || (pTopology_->Faces.empty() && pTopology_->Loops.empty() && pTopology_->Edges.empty());
    }

    ObjectMap _MakeRootPayload(IN const std::shared_ptr<iCAX::CAM::CLaserCamRootComponent>& pRoot_)
    {
        ObjectMap _Root;
        _Root["activeWorkpieceId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveWorkpieceID()) : std::string();
        _Root["activeToolAssemblyId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveToolAssemblyID()) : std::string();
        _Root["defaultCuttingLayerId"] = pRoot_ ? _UuidToString(pRoot_->GetDefaultCuttingLayerID()) : std::string();
        _Root["defaultVisibleLayerId"] = pRoot_ ? _UuidToString(pRoot_->GetDefaultVisibleLayerID()) : std::string();
        _Root["activeOrderPlanId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveOrderPlanID()) : std::string();
        _Root["latestSafetyCheckId"] = pRoot_ ? _UuidToString(pRoot_->GetLatestSafetyCheckID()) : std::string();
        _Root["activeSimulationId"] = pRoot_ ? _UuidToString(pRoot_->GetActiveSimulationID()) : std::string();
        _Root["programRootBlockId"] = pRoot_ ? _UuidToString(pRoot_->GetProgramRootBlockID()) : std::string();
        return _Root;
    }

    template <typename TComponent>
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _CollectEntitiesWithComponent(
        IN iCAX::Database::IRepository& Repository_)
    {
        std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
        auto _Entities = Repository_.FilterEntities([](std::shared_ptr<iCAX::Database::IEntity> pEntity_)
        {
            return pEntity_ && pEntity_->HasComponent(TComponent::S_ClassName);
        });

        _Result.reserve(_Entities.size());
        for (auto& _pEntity : _Entities)
        {
            auto _pComponent = _GetComponent<TComponent>(_pEntity);
            if (_pComponent)
            {
                _Result.emplace_back(_pEntity, _pComponent);
            }
        }
        return _Result;
    }

    template <typename TComponent>
    std::vector<iCAX::Data::uuid> _CollectEntityIDsWithComponent(IN iCAX::Database::IRepository& Repository_)
    {
        std::vector<iCAX::Data::uuid> _IDs;
        for (const auto& [_pEntity, _pComponent] : _CollectEntitiesWithComponent<TComponent>(Repository_))
        {
            if (_pEntity)
            {
                _IDs.push_back(_pEntity->GetID());
            }
        }
        return _IDs;
    }

    template <typename TComponent>
    void _DeleteEntitiesWithComponent(IN iCAX::Database::IRepository& Repository_)
    {
        auto _IDs = _CollectEntityIDsWithComponent<TComponent>(Repository_);
        for (const auto& _ID : _IDs)
        {
            std::string _strError;
            if (!Repository_.DeleteEntity(_ID, _strError))
            {
                throw std::runtime_error(_strError.empty() ? "Cam delete entity failed" : _strError);
            }
        }
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

    ObjectMap _MakeProgramNodeCommonPayload(IN const std::shared_ptr<iCAX::CAM::CCAMProgramNodeComponent>& pNode_)
    {
        ObjectMap _Node;
        _Node["name"] = pNode_ ? pNode_->GetName() : std::string();
        _Node["preInstructions"] = pNode_ ? pNode_->GetPreInstructions() : VariantArray{};
        _Node["postInstructions"] = pNode_ ? pNode_->GetPostInstructions() : VariantArray{};
        return _Node;
    }

    ObjectMap _MakeCuttingLayerPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CLaserCamCuttingLayerComponent>& pLayer_)
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
        IN const std::shared_ptr<iCAX::CAM::CLaserCamVisibleLayerComponent>& pLayer_)
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
        auto _Items = _CollectEntitiesWithComponent<iCAX::CAM::CLaserCamCuttingLayerComponent>(Repository_);
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
        auto _Items = _CollectEntitiesWithComponent<iCAX::CAM::CLaserCamVisibleLayerComponent>(Repository_);
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

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CLaserCamCuttingLayerComponent>> _CreateCuttingLayerEntity(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strName_,
        IN const std::string& strCuttingProcessID_,
        IN const std::string& strCuttingProcessName_,
        IN unsigned long long nOutputOrder_)
    {
        auto [_pEntity, _pLayer] = _CreateEntityWithComponent<iCAX::CAM::CLaserCamCuttingLayerComponent>(Repository_);
        _SetStringProperty(_pLayer, iCAX::CAM::CLaserCamCuttingLayerComponent::PropertyName_Name, strName_);
        _SetStringProperty(_pLayer, iCAX::CAM::CLaserCamCuttingLayerComponent::PropertyName_CuttingProcessID, strCuttingProcessID_);
        _SetStringProperty(_pLayer, iCAX::CAM::CLaserCamCuttingLayerComponent::PropertyName_CuttingProcessName, strCuttingProcessName_);
        _SetBoolProperty(_pLayer, iCAX::CAM::CLaserCamCuttingLayerComponent::PropertyName_Enabled, true);
        _SetUInt64Property(_pLayer, iCAX::CAM::CLaserCamCuttingLayerComponent::PropertyName_OutputOrder, nOutputOrder_);
        return { _pEntity, _pLayer };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CLaserCamVisibleLayerComponent>> _CreateVisibleLayerEntity(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strName_,
        IN const std::string& strColor_,
        IN unsigned long long nOrder_)
    {
        auto [_pEntity, _pLayer] = _CreateEntityWithComponent<iCAX::CAM::CLaserCamVisibleLayerComponent>(Repository_);
        _SetStringProperty(_pLayer, iCAX::CAM::CLaserCamVisibleLayerComponent::PropertyName_Name, strName_);
        _SetStringProperty(_pLayer, iCAX::CAM::CLaserCamVisibleLayerComponent::PropertyName_Color, strColor_);
        _SetBoolProperty(_pLayer, iCAX::CAM::CLaserCamVisibleLayerComponent::PropertyName_Visible, true);
        _SetBoolProperty(_pLayer, iCAX::CAM::CLaserCamVisibleLayerComponent::PropertyName_Locked, false);
        _SetUInt64Property(_pLayer, iCAX::CAM::CLaserCamVisibleLayerComponent::PropertyName_Order, nOrder_);
        return { _pEntity, _pLayer };
    }

    iCAX::Data::uuid _EnsureDefaultCuttingLayer(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CLaserCamRootComponent>(Repository_);
        if (!_pRoot->GetDefaultCuttingLayerID().is_nil())
        {
            auto _pEntity = Repository_.GetEntity(_pRoot->GetDefaultCuttingLayerID());
            if (_GetComponent<iCAX::CAM::CLaserCamCuttingLayerComponent>(_pEntity))
            {
                return _pRoot->GetDefaultCuttingLayerID();
            }
        }

        auto _Existing = _CollectEntitiesWithComponent<iCAX::CAM::CLaserCamCuttingLayerComponent>(Repository_);
        if (!_Existing.empty() && _Existing.front().first)
        {
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_DefaultCuttingLayerID, _Existing.front().first->GetID());
            return _Existing.front().first->GetID();
        }

        auto [_pLayerEntity, _pLayer] = _CreateCuttingLayerEntity(Repository_, "Default Cutting", "default", "Default Cutting Process", 0ull);
        _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_DefaultCuttingLayerID, _pLayerEntity->GetID());
        return _pLayerEntity->GetID();
    }

    iCAX::Data::uuid _EnsureDefaultVisibleLayer(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CLaserCamRootComponent>(Repository_);
        if (!_pRoot->GetDefaultVisibleLayerID().is_nil())
        {
            auto _pEntity = Repository_.GetEntity(_pRoot->GetDefaultVisibleLayerID());
            if (_GetComponent<iCAX::CAM::CLaserCamVisibleLayerComponent>(_pEntity))
            {
                return _pRoot->GetDefaultVisibleLayerID();
            }
        }

        auto _Existing = _CollectEntitiesWithComponent<iCAX::CAM::CLaserCamVisibleLayerComponent>(Repository_);
        if (!_Existing.empty() && _Existing.front().first)
        {
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_DefaultVisibleLayerID, _Existing.front().first->GetID());
            return _Existing.front().first->GetID();
        }

        auto [_pLayerEntity, _pLayer] = _CreateVisibleLayerEntity(Repository_, "Default Visible", "#2F80ED", 0ull);
        _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_DefaultVisibleLayerID, _pLayerEntity->GetID());
        return _pLayerEntity->GetID();
    }

    std::pair<iCAX::Data::uuid, iCAX::Data::uuid> _EnsureDefaultLayers(IN iCAX::Database::IRepository& Repository_)
    {
        return { _EnsureDefaultCuttingLayer(Repository_), _EnsureDefaultVisibleLayer(Repository_) };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CCAMBlockComponent>> _GetProgramRootBlock(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetComponent<iCAX::CAM::CLaserCamRootComponent>(Repository_);
        if (!_pRoot || _pRoot->GetProgramRootBlockID().is_nil())
        {
            return {};
        }

        auto _pEntity = Repository_.GetEntity(_pRoot->GetProgramRootBlockID());
        auto _pBlock = _GetComponent<iCAX::CAM::CCAMBlockComponent>(_pEntity);
        if (!_pEntity || !_pBlock)
        {
            return {};
        }
        return { _pEntity, _pBlock };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CCAMBlockComponent>> _CreateBlockEntity(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& strName_)
    {
        auto [_pEntity, _pBlock] = _CreateEntityWithComponent<iCAX::CAM::CCAMBlockComponent>(Repository_);
        _SetStringProperty(_pBlock, iCAX::CAM::CCAMProgramNodeComponent::PropertyName_Name, strName_);
        _SetVariantArrayProperty(_pBlock, iCAX::CAM::CCAMBlockComponent::PropertyName_Children, VariantArray{});
        return { _pEntity, _pBlock };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CCAMBlockComponent>> _EnsureProgramRootBlock(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pExistingEntity, _pExistingBlock] = _GetProgramRootBlock(Repository_);
        if (_pExistingEntity && _pExistingBlock)
        {
            return { _pExistingEntity, _pExistingBlock };
        }

        auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CLaserCamRootComponent>(Repository_);
        auto [_pBlockEntity, _pBlock] = _CreateBlockEntity(Repository_, "Program");
        _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_ProgramRootBlockID, _pBlockEntity->GetID());
        return { _pBlockEntity, _pBlock };
    }

    bool _BlockContainsChild(
        IN const std::shared_ptr<iCAX::CAM::CCAMBlockComponent>& pBlock_,
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
        IN const std::shared_ptr<iCAX::CAM::CCAMBlockComponent>& pBlock_,
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
        _SetVariantArrayProperty(pBlock_, iCAX::CAM::CCAMBlockComponent::PropertyName_Children, _Children);
    }

    void _AppendPathToProgramRoot(IN iCAX::Database::IRepository& Repository_, IN const iCAX::Data::uuid& PathEntityID_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _EnsureProgramRootBlock(Repository_);
        _AppendChildToBlock(_pRootBlock, kProgramChildKindPath, PathEntityID_);
    }

    void _ClearProgramRootBlockChildren(IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _EnsureProgramRootBlock(Repository_);
        _SetVariantArrayProperty(_pRootBlock, iCAX::CAM::CCAMBlockComponent::PropertyName_Children, VariantArray{});
    }

    void _DeleteNonRootProgramBlocks(IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pRootBlockEntity, _pRootBlock] = _GetProgramRootBlock(Repository_);
        const auto _RootBlockID = _pRootBlockEntity ? _pRootBlockEntity->GetID() : iCAX::Data::uuid();
        for (const auto& [_pEntity, _pBlock] : _CollectEntitiesWithComponent<iCAX::CAM::CCAMBlockComponent>(Repository_))
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

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CLaserWorkpieceComponent>> _GetActiveWorkpiece(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _pRoot = _GetComponent<iCAX::CAM::CLaserCamRootComponent>(Repository_);
        if (!_pRoot || _pRoot->GetActiveWorkpieceID().is_nil())
        {
            return {};
        }

        auto _pEntity = Repository_.GetEntity(_pRoot->GetActiveWorkpieceID());
        auto _pWorkpiece = _GetComponent<iCAX::CAM::CLaserWorkpieceComponent>(_pEntity);
        if (!_pEntity || !_pWorkpiece)
        {
            return {};
        }
        return { _pEntity, _pWorkpiece };
    }

    std::shared_ptr<iCAX::CAM::CCAMTopologyResource> _GetTopologyResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::shared_ptr<iCAX::CAM::CLaserWorkpieceComponent>& pWorkpiece_)
    {
        if (!pWorkpiece_ || pWorkpiece_->GetTopologyResourceID().empty())
        {
            return nullptr;
        }
        return Scene_.Resources().Get<iCAX::CAM::CCAMTopologyResource>(pWorkpiece_->GetTopologyResourceID());
    }

    const VariantArray& _GetTopologyArray(
        IN const iCAX::CAM::CCAMTopologyResource& Topology_,
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
        throw std::invalid_argument("Unsupported CAM topology kind: " + strKind_);
    }

    std::optional<ObjectMap> _FindTopologyObject(
        IN const iCAX::CAM::CCAMTopologyResource& Topology_,
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
        IN const iCAX::CAM::CCAMTopologyResource& Topology_,
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
        for (const auto& [_pEntity, _pPath] : _CollectEntitiesWithComponent<iCAX::CAM::CCAMPathComponent>(Repository_))
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
        const auto _PoseFieldResourceID = iCAX::CAM::MakeCAMPoseFieldResourceID(PathEntityID_);
        auto _pPoseField = std::make_shared<iCAX::CAM::CCAMPoseFieldResource>();
        _pPoseField->nVersion = 1;

        auto _PoseFieldInfo = _MakeResourceInfo(
            _PoseFieldResourceID,
            strPathName_ + " pose field",
            "cam.pose-field",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _pPoseField->nVersion);
        Scene_.Resources().Set<iCAX::CAM::CCAMPoseFieldResource>(_PoseFieldResourceID, _pPoseField, _PoseFieldInfo);
        return _PoseFieldResourceID;
    }

    std::shared_ptr<iCAX::CAM::CCAMPathComponent> _CreatePathEntity(
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
        auto [_pEntity, _pPath] = _CreateEntityWithComponent<iCAX::CAM::CCAMPathComponent>(_Repository);
        const auto _CurveResourceID = iCAX::CAM::MakeCAMPathCurveResourceID(_pEntity->GetID());

        auto _pCurve = std::make_shared<iCAX::CAM::CCAMPathCurveResource>();
        _pCurve->nVersion = 1;
        _pCurve->TopologyKind = strKind_;
        _pCurve->TopologyID = nTopologyID_;
        _pCurve->SourceTopology = TopologyObject_;

        auto _CurveInfo = _MakeResourceInfo(
            _CurveResourceID,
            _Label + " curve",
            "cam.path-curve",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _pCurve->nVersion);
        Scene_.Resources().Set<iCAX::CAM::CCAMPathCurveResource>(_CurveResourceID, _pCurve, _CurveInfo);
        const auto _PoseFieldResourceID = _CreatePoseFieldResource(Scene_, _pEntity->GetID(), _Label);

        _SetUuidProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_WorkpieceEntityID, WorkpieceEntityID_);
        _SetUuidProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_CuttingLayerID, _DefaultCuttingLayerID);
        _SetUuidProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_VisibleLayerID, _DefaultVisibleLayerID);
        _SetStringProperty(_pPath, iCAX::CAM::CCAMProgramNodeComponent::PropertyName_Name, _Label);
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_PathKind, "Cut");
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_TopologyKind, strKind_);
        _SetUInt64Property(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_TopologyID, nTopologyID_);
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_Source, strSource_);
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_Operation, _ResolveOperation(strKind_, _Role));
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_Role, _Role);
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_CurveResourceID, _CurveResourceID);
        _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_PoseFieldResourceID, _PoseFieldResourceID);
        _AppendPathToProgramRoot(_Repository, _pEntity->GetID());
        return _pPath;
    }

    ObjectMap _MakeSelectionPayload(IN const std::shared_ptr<iCAX::CAM::CCAMSelectionComponent>& pSelection_)
    {
        ObjectMap _Selection;
        _Selection["kind"] = pSelection_ ? pSelection_->GetSelectedKind() : std::string();
        _Selection["id"] = pSelection_ ? pSelection_->GetSelectedID() : 0ull;
        _Selection["label"] = pSelection_ ? pSelection_->GetSelectedLabel() : std::string();
        _Selection["hoverKind"] = pSelection_ ? pSelection_->GetHoverKind() : std::string();
        _Selection["hoverId"] = pSelection_ ? pSelection_->GetHoverID() : 0ull;
        return _Selection;
    }

    ObjectMap _MakeWorkpiecePayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CLaserWorkpieceComponent>& pWorkpiece_)
    {
        ObjectMap _Workpiece;
        _Workpiece["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Workpiece["name"] = pWorkpiece_ ? pWorkpiece_->GetName() : std::string();
        _Workpiece["sourcePath"] = pWorkpiece_ ? pWorkpiece_->GetSourcePath() : std::string();
        _Workpiece["modelResourceId"] = pWorkpiece_ ? pWorkpiece_->GetModelResourceID() : std::string();
        _Workpiece["topologyResourceId"] = pWorkpiece_ ? pWorkpiece_->GetTopologyResourceID() : std::string();
        _Workpiece["displayResourceId"] = pWorkpiece_ ? pWorkpiece_->GetDisplayResourceID() : std::string();
        _Workpiece["topologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetTopologyVersion() : 0ull;
        _Workpiece["isLoaded"] = pWorkpiece_ && !pWorkpiece_->GetSourcePath().empty();
        return _Workpiece;
    }

    ObjectMap _MakePathPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CCAMPathComponent>& pPath_,
        IN unsigned long long nOrderIndex_ = 0ull)
    {
        ObjectMap _Path;
        auto _Common = _MakeProgramNodeCommonPayload(std::dynamic_pointer_cast<iCAX::CAM::CCAMProgramNodeComponent>(pPath_));
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

    ObjectMap _MakeProgramBlockPayloadInternal(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CCAMBlockComponent>& pBlock_,
        IN OUT std::unordered_set<iCAX::Data::uuid>& VisitedBlocks_)
    {
        ObjectMap _Block = _MakeProgramNodeCommonPayload(std::dynamic_pointer_cast<iCAX::CAM::CCAMProgramNodeComponent>(pBlock_));
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
                auto _pPath = _GetComponent<iCAX::CAM::CCAMPathComponent>(_pChildEntity);
                if (_pChildEntity && _pPath)
                {
                    _Children.emplace_back(_MakePathPayload(_pChildEntity, _pPath, ++_OrderIndex));
                }
                continue;
            }

            if (_Reference->first == kProgramChildKindBlock)
            {
                auto _pChildBlock = _GetComponent<iCAX::CAM::CCAMBlockComponent>(_pChildEntity);
                if (_pChildEntity && _pChildBlock)
                {
                    _Children.emplace_back(_MakeProgramBlockPayloadInternal(Repository_, _pChildEntity, _pChildBlock, VisitedBlocks_));
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
        return _MakeProgramBlockPayloadInternal(Repository_, _pRootBlockEntity, _pRootBlock, _VisitedBlocks);
    }

    void _AppendPathsFromBlock(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::CAM::CCAMBlockComponent>& pBlock_,
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
                auto _pPath = _GetComponent<iCAX::CAM::CCAMPathComponent>(_pChildEntity);
                if (_pChildEntity && _pPath && VisitedPaths_.insert(_pChildEntity->GetID()).second)
                {
                    Result_.emplace_back(_MakePathPayload(_pChildEntity, _pPath, ++nOrderIndex_));
                }
                continue;
            }

            if (_Reference->first == kProgramChildKindBlock)
            {
                auto _pChildBlock = _GetComponent<iCAX::CAM::CCAMBlockComponent>(_pChildEntity);
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

    ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CCAMTopologyResource>& pTopology_)
    {
        ObjectMap _Status;
        _Status["hasTopology"] = !_IsTopologyResourceEmpty(pTopology_);
        _Status["version"] = pTopology_ ? pTopology_->nVersion : 0ull;
        _Status["faceCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Faces.size()) : 0ull;
        _Status["loopCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Loops.size()) : 0ull;
        _Status["edgeCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Edges.size()) : 0ull;
        return _Status;
    }

    Variant _BuildScenePayload(IN iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Repository = Scene_.Database();
        auto _pRoot = _GetComponent<iCAX::CAM::CLaserCamRootComponent>(_Repository);
        auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
        auto _pSelection = _GetComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
        auto _pTopology = _GetTopologyResource(Scene_, _pWorkpiece);
        auto _Workpiece = _MakeWorkpiecePayload(_pWorkpieceEntity, _pWorkpiece);

        ObjectMap _Scene;
        _Scene["root"] = _MakeRootPayload(_pRoot);
        _Scene["workpiece"] = _Workpiece;
        _Scene["model"] = _Workpiece;
        _Scene["topology"] = _MakeTopologyStatusPayload(_pTopology);
        _Scene["faces"] = _pTopology ? _pTopology->Faces : VariantArray{};
        _Scene["loops"] = _pTopology ? _pTopology->Loops : VariantArray{};
        _Scene["edges"] = _pTopology ? _pTopology->Edges : VariantArray{};
        _Scene["selection"] = _MakeSelectionPayload(_pSelection);
        _Scene["cuttingLayers"] = _MakeCuttingLayerArray(_Repository);
        _Scene["visibleLayers"] = _MakeVisibleLayerArray(_Repository);
        _Scene["program"] = _MakeProgramPayload(_Repository);
        _Scene["toolpaths"] = _MakePathArray(_Repository);
        return Variant(_Scene);
    }

    std::string _GetDisplayNameFromPath(IN const std::string& strSourcePath_)
    {
        if (strSourcePath_.empty())
        {
            return {};
        }

        std::filesystem::path _Path(strSourcePath_);
        auto _Name = _Path.filename().string();
        return _Name.empty() ? strSourcePath_ : _Name;
    }

    iCAX::Resource::CResourceInfo _MakeResourceInfo(
        IN const std::string& strSource_,
        IN const std::string& strName_,
        IN const std::string& strKind_,
        IN iCAX::Resource::EResourcePersistenceMode Persistence_,
        IN uint64_t nVersion_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = strSource_;
        _Info.Name = strName_;
        _Info.Persistence = Persistence_;
        _Info.nVersion = nVersion_;
        _Info.Metadata["kind"] = strKind_;
        return _Info;
    }

    uint64_t _NextResourceVersion(IN iCAX::Resource::CResourceLibrary& Resources_, IN const std::string& strResourceID_)
    {
        const auto _PreviousVersion = Resources_.GetVersion(strResourceID_);
        return _PreviousVersion == 0 ? 1 : _PreviousVersion + 1;
    }

    void _RegisterImportedModelResources(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strSourcePath_,
        OUT std::string& strModelResourceID_,
        OUT std::string& strTopologyResourceID_,
        OUT std::string& strDisplayResourceID_,
        OUT uint64_t& nTopologyVersion_)
    {
        if (strSourcePath_.empty())
        {
            throw std::invalid_argument("Cam ImportModel requires sourcePath");
        }

        auto& _Resources = Scene_.Resources();
        strModelResourceID_ = strSourcePath_;
        strTopologyResourceID_ = iCAX::CAM::MakeCAMTopologyResourceID(strModelResourceID_);
        strDisplayResourceID_ = iCAX::CAM::MakeCAMDisplayResourceID(strModelResourceID_);

        auto _pModelResource = std::make_shared<iCAX::CAM::CCAMImportedModelResource>();
        _pModelResource->SourcePath = strSourcePath_;
        _pModelResource->DisplayName = _GetDisplayNameFromPath(strSourcePath_);
        _pModelResource->nVersion = _NextResourceVersion(_Resources, strModelResourceID_);

        auto _ModelInfo = _MakeResourceInfo(
            strModelResourceID_,
            _pModelResource->DisplayName,
            "cam.imported-model",
            iCAX::Resource::EResourcePersistenceMode::External,
            _pModelResource->nVersion);
        _Resources.Set<iCAX::CAM::CCAMImportedModelResource>(strModelResourceID_, _pModelResource, _ModelInfo);

        auto _pTopologyResource = std::make_shared<iCAX::CAM::CCAMTopologyResource>();
        _pTopologyResource->nVersion = _NextResourceVersion(_Resources, strTopologyResourceID_);
        nTopologyVersion_ = _pTopologyResource->nVersion;

        auto _TopologyInfo = _MakeResourceInfo(
            strTopologyResourceID_,
            _pModelResource->DisplayName + " topology",
            "cam.topology",
            iCAX::Resource::EResourcePersistenceMode::RuntimeOnly,
            _pTopologyResource->nVersion);
        _Resources.Set<iCAX::CAM::CCAMTopologyResource>(strTopologyResourceID_, _pTopologyResource, _TopologyInfo);

        auto _DisplayInfo = _MakeResourceInfo(
            strDisplayResourceID_,
            _pModelResource->DisplayName + " display",
            "cam.display",
            iCAX::Resource::EResourcePersistenceMode::RuntimeOnly,
            nTopologyVersion_);
        _Resources.Register(strDisplayResourceID_, _DisplayInfo);
    }

    class CCAMCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CCAMCommandTarget()
            : CCommandTarget("Cam")
        {
            Bind("GetScene", &CCAMCommandTarget::HandleGetScene);
            Bind("ImportModel", &CCAMCommandTarget::HandleImportModel);
            Bind("PickTopology", &CCAMCommandTarget::HandlePickTopology);
            Bind("RecognizeLoops", &CCAMCommandTarget::HandleRecognizeLoops);
            Bind("AddSelectionPath", &CCAMCommandTarget::HandleAddSelectionPath);
            Bind("SetPoseField", &CCAMCommandTarget::HandleSetPoseField);
            Bind("ClearProgram", &CCAMCommandTarget::HandleClearProgram);
        }

    private:
        static iCAX::Command::CCommandResponse HandleGetScene(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            return _MakeResponse(_BuildScenePayload(_Scene));
        }

        static iCAX::Command::CCommandResponse HandleImportModel(
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto _Payload = _DecodeObjectPayload(Request_);
            auto _SourcePath = _GetOptionalString(_Payload, "sourcePath");
            if (_SourcePath.empty())
            {
                throw std::invalid_argument("Cam ImportModel requires sourcePath");
            }

            std::string _ModelResourceID;
            std::string _TopologyResourceID;
            std::string _DisplayResourceID;
            uint64_t _TopologyVersion = 0;
            _RegisterImportedModelResources(
                _Scene,
                _SourcePath,
                _ModelResourceID,
                _TopologyResourceID,
                _DisplayResourceID,
                _TopologyVersion);

            auto& _Repository = _Scene.Database();
            auto _Undo = _Repository.BeginUndoCommand("Import CAM model");
            auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CLaserCamRootComponent>(_Repository);
            auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);

            _DeleteEntitiesWithComponent<iCAX::CAM::CCAMPathComponent>(_Repository);
            _DeleteEntitiesWithComponent<iCAX::CAM::CCAMBlockComponent>(_Repository);
            _DeleteEntitiesWithComponent<iCAX::CAM::CLaserWorkpieceComponent>(_Repository);
            _DeleteEntitiesWithComponent<iCAX::CAM::CLaserCamCuttingLayerComponent>(_Repository);
            _DeleteEntitiesWithComponent<iCAX::CAM::CLaserCamVisibleLayerComponent>(_Repository);

            auto [_pProgramRootEntity, _pProgramRootBlock] = _EnsureProgramRootBlock(_Repository);
            _EnsureDefaultLayers(_Repository);
            auto [_pWorkpieceEntity, _pWorkpiece] = _CreateEntityWithComponent<iCAX::CAM::CLaserWorkpieceComponent>(_Repository);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_Name, _GetDisplayNameFromPath(_SourcePath));
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_SourcePath, _SourcePath);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_ModelResourceID, _ModelResourceID);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_TopologyResourceID, _TopologyResourceID);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_DisplayResourceID, _DisplayResourceID);
            _SetUInt64Property(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_TopologyVersion, _TopologyVersion);
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_ActiveWorkpieceID, _pWorkpieceEntity->GetID());
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_ActiveOrderPlanID, iCAX::Data::uuid());
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_LatestSafetyCheckID, iCAX::Data::uuid());
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_ActiveSimulationID, iCAX::Data::uuid());
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedKind, std::string());
            _SetUInt64Property(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedID, 0ull);
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedLabel, std::string());
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Scene));
        }

        static iCAX::Command::CCommandResponse HandlePickTopology(
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto _Payload = _DecodeObjectPayload(Request_);
            auto _Kind = _GetOptionalString(_Payload, "kind");
            const auto _ID = _GetOptionalUInt64(_Payload, "id");
            if (_Kind.empty() || _ID == 0ull)
            {
                throw std::invalid_argument("Cam PickTopology requires kind and id");
            }

            auto& _Repository = _Scene.Database();
            auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
            auto _pTopology = _GetTopologyResource(_Scene, _pWorkpiece);
            if (!_pTopology)
            {
                throw std::invalid_argument("Cam topology resource is not available");
            }
            auto _TopologyObject = _FindTopologyObject(*_pTopology, _Kind, _ID);
            if (!_TopologyObject.has_value())
            {
                throw std::invalid_argument("Cam topology target does not exist");
            }

            auto _Label = _GetOptionalString(_Payload, "label", _ResolveTopologyLabel(*_pTopology, _Kind, _ID));
            if (_Label.empty())
            {
                _Label = _Kind + " " + std::to_string(_ID);
            }

            auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedKind, _Kind);
            _SetUInt64Property(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedID, _ID);
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedLabel, _Label);
            return _MakeResponse(_BuildScenePayload(_Scene));
        }

        static iCAX::Command::CCommandResponse HandleRecognizeLoops(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto& _Repository = _Scene.Database();
            auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
            if (!_pWorkpieceEntity || !_pWorkpiece)
            {
                throw std::invalid_argument("Cam active workpiece is not available");
            }
            auto _pTopology = _GetTopologyResource(_Scene, _pWorkpiece);
            if (!_pTopology)
            {
                throw std::invalid_argument("Cam topology resource is not available");
            }

            auto _Undo = _Repository.BeginUndoCommand("Recognize CAM loops");
            for (const auto& _Loop : _pTopology->Loops)
            {
                if (!_Loop.Is<ObjectMap>())
                {
                    continue;
                }
                const auto _LoopObject = _Loop.To<ObjectMap>();
                const auto _Role = _GetObjectString(_LoopObject, "role");
                const auto _TopologyID = _GetObjectUInt64(_LoopObject, "id");
                if (_TopologyID == 0ull || (_Role != "hole" && _Role != "cut"))
                {
                    continue;
                }
                if (_HasPathForTopology(_Repository, _pWorkpieceEntity->GetID(), kTopologyKindLoop, _TopologyID))
                {
                    continue;
                }
                _CreatePathEntity(_Scene, _pWorkpieceEntity->GetID(), kTopologyKindLoop, _TopologyID, _LoopObject, "auto");
            }
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Scene));
        }

        static iCAX::Command::CCommandResponse HandleAddSelectionPath(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto& _Repository = _Scene.Database();
            auto _pSelection = _GetComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
            if (!_pSelection || _pSelection->GetSelectedKind().empty() || _pSelection->GetSelectedID() == 0ull)
            {
                throw std::invalid_argument("Cam selection is empty");
            }

            auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
            if (!_pWorkpieceEntity || !_pWorkpiece)
            {
                throw std::invalid_argument("Cam active workpiece is not available");
            }
            auto _pTopology = _GetTopologyResource(_Scene, _pWorkpiece);
            if (!_pTopology)
            {
                throw std::invalid_argument("Cam topology resource is not available");
            }
            auto _TopologyObject = _FindTopologyObject(*_pTopology, _pSelection->GetSelectedKind(), _pSelection->GetSelectedID());
            if (!_TopologyObject.has_value())
            {
                throw std::invalid_argument("Cam selected topology target does not exist");
            }

            auto _Undo = _Repository.BeginUndoCommand("Add CAM path target");
            if (!_HasPathForTopology(_Repository, _pWorkpieceEntity->GetID(), _pSelection->GetSelectedKind(), _pSelection->GetSelectedID()))
            {
                _CreatePathEntity(
                    _Scene,
                    _pWorkpieceEntity->GetID(),
                    _pSelection->GetSelectedKind(),
                    _pSelection->GetSelectedID(),
                    *_TopologyObject,
                    "manual");
            }
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Scene));
        }

        static iCAX::Command::CCommandResponse HandleSetPoseField(
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto _Payload = _DecodeObjectPayload(Request_);
            const auto _PathIDText = _GetOptionalString(_Payload, "pathEntityId");
            const auto _PathID = _ParseRequiredUuid(_PathIDText, "pathEntityId");

            auto _SamplesIter = _Payload.find("samples");
            if (_SamplesIter == _Payload.end() || !_SamplesIter->second.Is<VariantArray>())
            {
                throw std::invalid_argument("Cam SetPoseField requires samples array");
            }
            auto _Samples = _NormalizePoseSamples(_SamplesIter->second.To<VariantArray>());

            auto& _Repository = _Scene.Database();
            auto _pPathEntity = _Repository.GetEntity(_PathID);
            auto _pPath = _GetComponent<iCAX::CAM::CCAMPathComponent>(_pPathEntity);
            if (!_pPathEntity || !_pPath)
            {
                throw std::invalid_argument("Cam SetPoseField path does not exist");
            }

            auto& _Resources = _Scene.Resources();
            auto _PoseFieldResourceID = _pPath->GetPoseFieldResourceID();
            if (_PoseFieldResourceID.empty())
            {
                _PoseFieldResourceID = iCAX::CAM::MakeCAMPoseFieldResourceID(_PathID);
            }

            auto _pPoseField = std::make_shared<iCAX::CAM::CCAMPoseFieldResource>();
            _pPoseField->nVersion = _NextResourceVersion(_Resources, _PoseFieldResourceID);
            _pPoseField->Samples = std::move(_Samples);

            auto _PoseFieldInfo = _MakeResourceInfo(
                _PoseFieldResourceID,
                _pPath->GetName() + " pose field",
                "cam.pose-field",
                iCAX::Resource::EResourcePersistenceMode::Embedded,
                _pPoseField->nVersion);
            _Resources.Set<iCAX::CAM::CCAMPoseFieldResource>(_PoseFieldResourceID, _pPoseField, _PoseFieldInfo);

            auto _Undo = _Repository.BeginUndoCommand("Set CAM pose field");
            if (_pPath->GetPoseFieldResourceID() != _PoseFieldResourceID)
            {
                _SetStringProperty(_pPath, iCAX::CAM::CCAMPathComponent::PropertyName_PoseFieldResourceID, _PoseFieldResourceID);
            }
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Scene));
        }

        static iCAX::Command::CCommandResponse HandleClearProgram(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto& _Repository = _Scene.Database();
            auto _Undo = _Repository.BeginUndoCommand("Clear CAM program");
            _DeleteEntitiesWithComponent<iCAX::CAM::CCAMPathComponent>(_Repository);
            _DeleteNonRootProgramBlocks(_Repository);
            _ClearProgramRootBlockChildren(_Repository);
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Scene));
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CCAMCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CCAMCommandTarget)
