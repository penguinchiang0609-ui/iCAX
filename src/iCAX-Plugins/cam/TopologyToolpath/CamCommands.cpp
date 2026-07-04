#include "pch.h"
#include "TopologyToolpathComponents.h"
#include "TopologyToolpathResources.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"
#include "Data/VariantSerializer.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProjectContext/IProjectContext.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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

    iCAX::Project::IProjectContext& _RequireProjectContext(IN iCAX::Project::IProjectContext* pProjectContext_)
    {
        if (!pProjectContext_)
        {
            throw std::invalid_argument("Cam command must be sent to a project mailbox");
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

    VariantArray _ParseToolpathTargets(IN const std::shared_ptr<iCAX::CAM::CCAMToolpathListComponent>& pToolpathList_)
    {
        if (!pToolpathList_ || pToolpathList_->GetTargetsText().empty())
        {
            return {};
        }

        auto _Targets = iCAX::Data::VariantSerializer::Deserialize(pToolpathList_->GetTargetsText());
        if (!_Targets.Is<VariantArray>())
        {
            throw std::runtime_error("Cam toolpath list component payload is not an array");
        }
        return _Targets.To<VariantArray>();
    }

    std::string _SerializeToolpathTargets(IN const VariantArray& Targets_)
    {
        return iCAX::Data::VariantSerializer::Serialize(Variant(Targets_));
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

    std::shared_ptr<iCAX::CAM::CCAMTopologyResource> _GetTopologyResource(
        IN iCAX::Project::IProjectContext& Project_,
        IN const std::shared_ptr<iCAX::CAM::CCAMModelComponent>& pModel_)
    {
        if (!pModel_ || pModel_->GetTopologyResourceID().empty())
        {
            return nullptr;
        }
        return Project_.Resources().Get<iCAX::CAM::CCAMTopologyResource>(pModel_->GetTopologyResourceID());
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

    bool _ContainsToolpathTopology(
        IN const VariantArray& Targets_,
        IN const std::string& strKind_,
        IN unsigned long long nTopologyID_)
    {
        for (const auto& _Item : Targets_)
        {
            if (!_Item.Is<ObjectMap>())
            {
                continue;
            }

            const auto _Object = _Item.To<ObjectMap>();
            const auto _Kind = _GetObjectString(_Object, "topologyKind");
            const auto _ID = _GetObjectUInt64(_Object, "topologyId");
            if (_Kind == strKind_ && _ID == nTopologyID_)
            {
                return true;
            }
        }
        return false;
    }

    unsigned long long _NextToolpathTargetID(IN const VariantArray& Targets_)
    {
        unsigned long long _NextID = 1;
        for (const auto& _Item : Targets_)
        {
            if (!_Item.Is<ObjectMap>())
            {
                continue;
            }
            const auto _Object = _Item.To<ObjectMap>();
            _NextID = std::max(_NextID, _GetObjectUInt64(_Object, "id") + 1ull);
        }
        return _NextID;
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

    ObjectMap _MakeToolpathTarget(
        IN unsigned long long nID_,
        IN const std::string& strKind_,
        IN unsigned long long nTopologyID_,
        IN const ObjectMap& TopologyObject_,
        IN const std::string& strSource_)
    {
        const auto _Label = _GetObjectString(TopologyObject_, "label", strKind_ + " " + std::to_string(nTopologyID_));
        const auto _Role = _GetObjectString(TopologyObject_, "role");

        ObjectMap _Target;
        _Target["id"] = nID_;
        _Target["topologyKind"] = strKind_;
        _Target["topologyId"] = nTopologyID_;
        _Target["label"] = _Label;
        _Target["source"] = strSource_;
        _Target["operation"] = _ResolveOperation(strKind_, _Role);
        if (!_Role.empty())
        {
            _Target["role"] = _Role;
        }
        return _Target;
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

    ObjectMap _MakeModelPayload(IN const std::shared_ptr<iCAX::CAM::CCAMModelComponent>& pModel_)
    {
        ObjectMap _Model;
        _Model["sourcePath"] = pModel_ ? pModel_->GetSourcePath() : std::string();
        _Model["modelResourceId"] = pModel_ ? pModel_->GetModelResourceID() : std::string();
        _Model["topologyResourceId"] = pModel_ ? pModel_->GetTopologyResourceID() : std::string();
        _Model["displayResourceId"] = pModel_ ? pModel_->GetDisplayResourceID() : std::string();
        _Model["topologyVersion"] = pModel_ ? pModel_->GetTopologyVersion() : 0ull;
        _Model["isLoaded"] = pModel_ && !pModel_->GetSourcePath().empty();
        return _Model;
    }

    ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CCAMTopologyResource>& pTopology_)
    {
        ObjectMap _Status;
        _Status["hasTopology"] = pTopology_ && !pTopology_->IsEmpty();
        _Status["version"] = pTopology_ ? pTopology_->nVersion : 0ull;
        _Status["faceCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Faces.size()) : 0ull;
        _Status["loopCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Loops.size()) : 0ull;
        _Status["edgeCount"] = pTopology_ ? static_cast<unsigned long long>(pTopology_->Edges.size()) : 0ull;
        return _Status;
    }

    Variant _BuildScenePayload(IN iCAX::Project::IProjectContext& Project_)
    {
        auto& _Repository = Project_.Database();
        auto _pModel = _GetComponent<iCAX::CAM::CCAMModelComponent>(_Repository);
        auto _pSelection = _GetComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
        auto _pToolpathList = _GetComponent<iCAX::CAM::CCAMToolpathListComponent>(_Repository);
        auto _pTopology = _GetTopologyResource(Project_, _pModel);

        ObjectMap _Scene;
        _Scene["model"] = _MakeModelPayload(_pModel);
        _Scene["topology"] = _MakeTopologyStatusPayload(_pTopology);
        _Scene["faces"] = _pTopology ? _pTopology->Faces : VariantArray{};
        _Scene["loops"] = _pTopology ? _pTopology->Loops : VariantArray{};
        _Scene["edges"] = _pTopology ? _pTopology->Edges : VariantArray{};
        _Scene["selection"] = _MakeSelectionPayload(_pSelection);
        _Scene["toolpaths"] = _ParseToolpathTargets(_pToolpathList);
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
        IN iCAX::Project::IProjectContext& Project_,
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

        auto& _Resources = Project_.Resources();
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
            Bind("AddSelectionToToolpathList", &CCAMCommandTarget::HandleAddSelectionToToolpathList);
            Bind("ClearToolpathList", &CCAMCommandTarget::HandleClearToolpathList);
        }

    private:
        static iCAX::Command::CCommandResponse HandleGetScene(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext* pProjectContext_)
        {
            auto& _Project = _RequireProjectContext(pProjectContext_);
            return _MakeResponse(_BuildScenePayload(_Project));
        }

        static iCAX::Command::CCommandResponse HandleImportModel(
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext* pProjectContext_)
        {
            auto& _Project = _RequireProjectContext(pProjectContext_);
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
                _Project,
                _SourcePath,
                _ModelResourceID,
                _TopologyResourceID,
                _DisplayResourceID,
                _TopologyVersion);

            auto& _Repository = _Project.Database();
            auto _Undo = _Repository.BeginUndoCommand("Import CAM model");
            auto _pModel = _GetOrCreateComponent<iCAX::CAM::CCAMModelComponent>(_Repository);
            auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
            auto _pToolpathList = _GetOrCreateComponent<iCAX::CAM::CCAMToolpathListComponent>(_Repository);

            _SetStringProperty(_pModel, iCAX::CAM::CCAMModelComponent::PropertyName_SourcePath, _SourcePath);
            _SetStringProperty(_pModel, iCAX::CAM::CCAMModelComponent::PropertyName_ModelResourceID, _ModelResourceID);
            _SetStringProperty(_pModel, iCAX::CAM::CCAMModelComponent::PropertyName_TopologyResourceID, _TopologyResourceID);
            _SetStringProperty(_pModel, iCAX::CAM::CCAMModelComponent::PropertyName_DisplayResourceID, _DisplayResourceID);
            _SetUInt64Property(_pModel, iCAX::CAM::CCAMModelComponent::PropertyName_TopologyVersion, _TopologyVersion);
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedKind, std::string());
            _SetUInt64Property(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedID, 0ull);
            _SetStringProperty(_pSelection, iCAX::CAM::CCAMSelectionComponent::PropertyName_SelectedLabel, std::string());
            _SetStringProperty(_pToolpathList, iCAX::CAM::CCAMToolpathListComponent::PropertyName_TargetsText, _SerializeToolpathTargets({}));
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Project));
        }

        static iCAX::Command::CCommandResponse HandlePickTopology(
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext* pProjectContext_)
        {
            auto& _Project = _RequireProjectContext(pProjectContext_);
            auto _Payload = _DecodeObjectPayload(Request_);
            auto _Kind = _GetOptionalString(_Payload, "kind");
            const auto _ID = _GetOptionalUInt64(_Payload, "id");
            if (_Kind.empty() || _ID == 0ull)
            {
                throw std::invalid_argument("Cam PickTopology requires kind and id");
            }

            auto& _Repository = _Project.Database();
            auto _pModel = _GetComponent<iCAX::CAM::CCAMModelComponent>(_Repository);
            auto _pTopology = _GetTopologyResource(_Project, _pModel);
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
            return _MakeResponse(_BuildScenePayload(_Project));
        }

        static iCAX::Command::CCommandResponse HandleRecognizeLoops(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext* pProjectContext_)
        {
            auto& _Project = _RequireProjectContext(pProjectContext_);
            auto& _Repository = _Project.Database();
            auto _pModel = _GetComponent<iCAX::CAM::CCAMModelComponent>(_Repository);
            auto _pTopology = _GetTopologyResource(_Project, _pModel);
            if (!_pTopology)
            {
                throw std::invalid_argument("Cam topology resource is not available");
            }

            auto _Undo = _Repository.BeginUndoCommand("Recognize CAM loops");
            auto _pToolpathList = _GetOrCreateComponent<iCAX::CAM::CCAMToolpathListComponent>(_Repository);
            auto _Targets = _ParseToolpathTargets(_pToolpathList);
            auto _NextID = _NextToolpathTargetID(_Targets);

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
                if (_ContainsToolpathTopology(_Targets, kTopologyKindLoop, _TopologyID))
                {
                    continue;
                }
                _Targets.emplace_back(_MakeToolpathTarget(_NextID++, kTopologyKindLoop, _TopologyID, _LoopObject, "auto"));
            }

            _SetStringProperty(_pToolpathList, iCAX::CAM::CCAMToolpathListComponent::PropertyName_TargetsText, _SerializeToolpathTargets(_Targets));
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Project));
        }

        static iCAX::Command::CCommandResponse HandleAddSelectionToToolpathList(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext* pProjectContext_)
        {
            auto& _Project = _RequireProjectContext(pProjectContext_);
            auto& _Repository = _Project.Database();
            auto _pSelection = _GetComponent<iCAX::CAM::CCAMSelectionComponent>(_Repository);
            if (!_pSelection || _pSelection->GetSelectedKind().empty() || _pSelection->GetSelectedID() == 0ull)
            {
                throw std::invalid_argument("Cam selection is empty");
            }

            auto _pModel = _GetComponent<iCAX::CAM::CCAMModelComponent>(_Repository);
            auto _pTopology = _GetTopologyResource(_Project, _pModel);
            if (!_pTopology)
            {
                throw std::invalid_argument("Cam topology resource is not available");
            }
            auto _TopologyObject = _FindTopologyObject(*_pTopology, _pSelection->GetSelectedKind(), _pSelection->GetSelectedID());
            if (!_TopologyObject.has_value())
            {
                throw std::invalid_argument("Cam selected topology target does not exist");
            }

            auto _Undo = _Repository.BeginUndoCommand("Add CAM toolpath target");
            auto _pToolpathList = _GetOrCreateComponent<iCAX::CAM::CCAMToolpathListComponent>(_Repository);
            auto _Targets = _ParseToolpathTargets(_pToolpathList);
            if (!_ContainsToolpathTopology(_Targets, _pSelection->GetSelectedKind(), _pSelection->GetSelectedID()))
            {
                const auto _TargetID = _NextToolpathTargetID(_Targets);
                _Targets.emplace_back(_MakeToolpathTarget(
                    _TargetID,
                    _pSelection->GetSelectedKind(),
                    _pSelection->GetSelectedID(),
                    *_TopologyObject,
                    "manual"));
                _SetStringProperty(_pToolpathList, iCAX::CAM::CCAMToolpathListComponent::PropertyName_TargetsText, _SerializeToolpathTargets(_Targets));
            }
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Project));
        }

        static iCAX::Command::CCommandResponse HandleClearToolpathList(
            IN const iCAX::Command::CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext* pProjectContext_)
        {
            auto& _Project = _RequireProjectContext(pProjectContext_);
            auto& _Repository = _Project.Database();
            auto _Undo = _Repository.BeginUndoCommand("Clear CAM toolpath list");
            auto _pToolpathList = _GetOrCreateComponent<iCAX::CAM::CCAMToolpathListComponent>(_Repository);
            _SetStringProperty(_pToolpathList, iCAX::CAM::CCAMToolpathListComponent::PropertyName_TargetsText, _SerializeToolpathTargets({}));
            _Undo->End();

            return _MakeResponse(_BuildScenePayload(_Project));
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CCAMCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CCAMCommandTarget)
