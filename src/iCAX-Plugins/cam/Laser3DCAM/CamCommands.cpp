#include "pch.h"
#include "ToolpathComponents.h"
#include "ToolpathResourceKeys.h"
#include "ToolpathResources.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"
#include "ApplicationContext/IApplicationContext.h"
#include "Data/uuid.h"
#include "Data/VariantSerializer.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "GeometryData/GeometryData.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
    constexpr size_t kMaxInlineTopologyPickItems = 10000;

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
            throw std::invalid_argument("Cam command must be sent to a scene mailbox");
        }
        return *pSceneContext_;
    }

    iCAX::Product::IProductContext& _RequireProductContext(IN iCAX::Product::IProductContext* pProductContext_)
    {
        if (!pProductContext_)
        {
            throw std::invalid_argument("Cam command requires product context");
        }
        return *pProductContext_;
    }

    iCAX::Project::IProjectContext& _RequireProjectContext(IN iCAX::Project::IProjectContext* pProjectContext_)
    {
        if (!pProjectContext_)
        {
            throw std::invalid_argument("Cam command requires project context");
        }
        return *pProjectContext_;
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

    double _GetOptionalDouble(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        IN double dDefault_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return dDefault_;
        }
        return _ToDouble(_Iter->second, strName_);
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

    iCAX::CAM::SCAMPoseSample _NormalizePoseSample(IN const Variant& Sample_, IN size_t nSampleIndex_)
    {
        if (!Sample_.Is<ObjectMap>())
        {
            throw std::invalid_argument("Cam pose sample must be an object at index: " + std::to_string(nSampleIndex_));
        }

        const auto _Input = Sample_.To<ObjectMap>();
        iCAX::CAM::SCAMPoseSample _Sample;
        _Sample.nSegmentIndex = _GetObjectUInt64(_Input, "segmentIndex");
        _Sample.dSegmentU = _GetObjectDouble(_Input, "segmentU");
        _Sample.BeamDirection = _ReadDirection3(_Input, "beamDirection");
        return _Sample;
    }

    std::vector<iCAX::CAM::SCAMPoseSample> _NormalizePoseSamples(IN const VariantArray& Samples_)
    {
        std::vector<iCAX::CAM::SCAMPoseSample> _Result;
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
        _Workpiece["brepResourceId"] = pWorkpiece_ ? pWorkpiece_->GetBRepResourceID() : std::string();
        _Workpiece["topologyResourceId"] = pWorkpiece_ ? pWorkpiece_->GetTopologyResourceID() : std::string();
        _Workpiece["displayResourceId"] = pWorkpiece_ ? pWorkpiece_->GetDisplayResourceID() : std::string();
        _Workpiece["topologyVersion"] = pWorkpiece_ ? pWorkpiece_->GetTopologyVersion() : 0ull;
        _Workpiece["isLoaded"] = pWorkpiece_ && !pWorkpiece_->GetSourcePath().empty();
        return _Workpiece;
    }

    VariantArray _MakeWorkpieceArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Workpieces;
        for (const auto& [_pEntity, _pWorkpiece] : _CollectEntitiesWithComponent<iCAX::CAM::CLaserWorkpieceComponent>(Repository_))
        {
            _Workpieces.emplace_back(_MakeWorkpiecePayload(_pEntity, _pWorkpiece));
        }
        return _Workpieces;
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
        _Scene["workpieces"] = _MakeWorkpieceArray(_Repository);
        _Scene["model"] = _Workpiece;
        _Scene["topology"] = _MakeTopologyStatusPayload(_pTopology);
        _Scene["faces"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Faces) : VariantArray{};
        _Scene["loops"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Loops) : VariantArray{};
        _Scene["edges"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Edges) : VariantArray{};
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

    struct SCAMImportedCadResources final
    {
        std::string ModelResourceID;
        std::string BRepResourceID;
        std::string TopologyResourceID;
        uint64_t nTopologyVersion = 0;
    };

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

    std::shared_ptr<iCAX::CAM::CCAMTopologyResource> _MakeTopologyResourceFromBRep(
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

        auto _pTopology = std::make_shared<iCAX::CAM::CCAMTopologyResource>();
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

    SCAMImportedCadResources _ImportCadModel(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strSourcePath_,
        IN double dTolerance_)
    {
        iCAX::Resource::CResourceImportRequest _Request;
        _Request.SourcePath = strSourcePath_;
        _Request.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        _Request.Options["tolerance"] = std::to_string(dTolerance_);

        auto& _Resources = Scene_.Resources();
        iCAX::Resource::CResourceImportResult _Result;
        auto _pBRep = _Resources.Import<iCAX::GeometryData::BRepModel>(_Request, &_Result);
        if (!_Result.IsOK())
        {
            throw std::runtime_error(_Result.Error.empty() ? "CAD resource import failed" : _Result.Error);
        }

        SCAMImportedCadResources _Imported;
        _Imported.ModelResourceID = _FindImportedResourceID(_Result, "source");
        if (_Imported.ModelResourceID.empty())
        {
            _Imported.ModelResourceID = _Result.PrimaryResourceID;
        }
        _Imported.BRepResourceID = _FindImportedResourceID(_Result, "geometry.brep");
        _RequireImportedResource(Scene_, _Imported.ModelResourceID, "modelResourceId");
        _RequireImportedResource(Scene_, _Imported.BRepResourceID, "brepResourceId");

        if (!_pBRep)
        {
            throw std::runtime_error("Resource import returned BRep resource with unexpected runtime type");
        }

        _Imported.TopologyResourceID = iCAX::CAM::MakeCAMTopologyResourceID(_Imported.ModelResourceID);
        _Imported.nTopologyVersion = _NextResourceVersion(_Resources, _Imported.TopologyResourceID);
        auto _pTopology = _MakeTopologyResourceFromBRep(*_pBRep, _GetDisplayNameFromPath(strSourcePath_), _Imported.nTopologyVersion);
        _pTopology->Metadata["importMode"] = _Result.Metadata.contains("importer") ? _Result.Metadata.at("importer") : std::string("resource-import");
        _pTopology->Metadata["sourcePath"] = strSourcePath_;
        _pTopology->Metadata["sourceResourceId"] = _Imported.ModelResourceID;
        _pTopology->Metadata["brepResourceId"] = _Imported.BRepResourceID;
        if (auto _ContentHash = _Result.Metadata.find("contentHash"); _ContentHash != _Result.Metadata.end())
        {
            _pTopology->Metadata["contentHash"] = _ContentHash->second;
        }

        auto _TopologyInfo = _MakeResourceInfo(
            _Imported.TopologyResourceID,
            _GetDisplayNameFromPath(strSourcePath_) + " topology",
            "cam.topology",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Imported.nTopologyVersion);
        _TopologyInfo.Metadata["sourceResourceId"] = _Imported.ModelResourceID;
        _TopologyInfo.Metadata["brepResourceId"] = _Imported.BRepResourceID;
        _Resources.Set<iCAX::CAM::CCAMTopologyResource>(_Imported.TopologyResourceID, _pTopology, _TopologyInfo);

        _RequireImportedResource(Scene_, _Imported.TopologyResourceID, "topologyResourceId");
        return _Imported;
    }

    class CCAMCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CCAMCommandTarget()
            : CCommandTarget("Cam")
        {
            Bind("GetScene", &CCAMCommandTarget::HandleGetScene);
            Bind("ImportModel", &CCAMCommandTarget::HandleImportModel);
            Bind("SetActiveWorkpiece", &CCAMCommandTarget::HandleSetActiveWorkpiece);
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
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            (void)_RequireProductContext(pProductContext_);
            (void)_RequireProjectContext(pProjectContext_);
            auto _Payload = _DecodeObjectPayload(Request_);
            auto _SourcePath = _GetOptionalString(_Payload, "sourcePath");
            if (_SourcePath.empty())
            {
                throw std::invalid_argument("Cam ImportModel requires sourcePath");
            }
            if (!_IsSupportedWorkpieceModelPath(_SourcePath))
            {
                throw std::invalid_argument("Cam ImportModel only supports STEP/STP and IGS/IGES workpiece files");
            }

            const auto _Tolerance = _GetOptionalDouble(_Payload, "tolerance", 0.001);
            if (_Tolerance <= 0.0)
            {
                throw std::invalid_argument("Cam ImportModel tolerance must be greater than zero");
            }
            const auto _ImportResult = _ImportCadModel(_Scene, _SourcePath, _Tolerance);

            auto& _Repository = _Scene.Database();
            auto _Undo = _Repository.BeginUndoCommand("Import CAM workpiece");
            auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CLaserCamRootComponent>(_Repository);
            auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);

            auto [_pProgramRootEntity, _pProgramRootBlock] = _EnsureProgramRootBlock(_Repository);
            _EnsureDefaultLayers(_Repository);
            auto [_pWorkpieceEntity, _pWorkpiece] = _CreateEntityWithComponent<iCAX::CAM::CLaserWorkpieceComponent>(_Repository);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_Name, _GetDisplayNameFromPath(_SourcePath));
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_SourcePath, _SourcePath);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_ModelResourceID, _ImportResult.ModelResourceID);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_BRepResourceID, _ImportResult.BRepResourceID);
            _SetStringProperty(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_TopologyResourceID, _ImportResult.TopologyResourceID);
            _SetUInt64Property(_pWorkpiece, iCAX::CAM::CLaserWorkpieceComponent::PropertyName_TopologyVersion, _ImportResult.nTopologyVersion);
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

        static iCAX::Command::CCommandResponse HandleSetActiveWorkpiece(
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext* pSceneContext_)
        {
            auto& _Scene = _RequireSceneContext(pSceneContext_);
            auto _Payload = _DecodeObjectPayload(Request_);
            auto _WorkpieceIDText = _GetOptionalString(_Payload, "workpieceEntityId");
            if (_WorkpieceIDText.empty())
            {
                _WorkpieceIDText = _GetOptionalString(_Payload, "entityId");
            }
            const auto _WorkpieceID = _ParseRequiredUuid(_WorkpieceIDText, "workpieceEntityId");

            auto& _Repository = _Scene.Database();
            auto _pWorkpieceEntity = _Repository.GetEntity(_WorkpieceID);
            auto _pWorkpiece = _GetComponent<iCAX::CAM::CLaserWorkpieceComponent>(_pWorkpieceEntity);
            if (!_pWorkpieceEntity || !_pWorkpiece)
            {
                throw std::invalid_argument("Cam SetActiveWorkpiece target does not exist");
            }

            auto _Undo = _Repository.BeginUndoCommand("Set active CAM workpiece");
            auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CLaserCamRootComponent>(_Repository);
            auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
            _SetUuidProperty(_pRoot, iCAX::CAM::CLaserCamRootComponent::PropertyName_ActiveWorkpieceID, _WorkpieceID);
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_HoverKind, std::string());
            _SetUInt64Property(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_HoverID, 0ull);
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
