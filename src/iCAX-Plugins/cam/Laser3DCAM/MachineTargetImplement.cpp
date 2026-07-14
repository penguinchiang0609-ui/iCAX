#include "pch.h"
#include "MachineTargetImplement.h"

#include "MachineDescriptionLoader.h"
#include "MachineResourceKeys.h"
#include "MachineTransformConstraints.h"
#include "GeometryData/GeometryData.h"
#include "RenderData/RenderData.h"
#include "RenderInteraction/RenderInteraction.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceLibrary.h"
#include "ToolpathTargetImplement.h"
#include "Transform/Transform.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace iCAX::CAM::Commands::Internal
{
namespace
{
    constexpr double kMachineDescriptionLengthToWorld = 1000.0;
}

    bool _IsSupportedMachineDescriptionPath(IN const std::string& strSourcePath_)
    {
        return iCAX::CAM::IsSupportedMachineDescriptionPath(strSourcePath_);
    }

    std::optional<ObjectMap> _TryGetObjectMapField(IN const ObjectMap& Object_, IN const std::string& strName_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<ObjectMap>())
        {
            return std::nullopt;
        }
        return _Iter->second.To<ObjectMap>();
    }

    double _GetObjectDoubleOrDefault(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN double dDefault_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return dDefault_;
        }
        try
        {
            return _ToDouble(_Iter->second, strName_);
        }
        catch (const std::exception&)
        {
            return dDefault_;
        }
    }

    bool _IsMovableMachineJoint(IN const std::string& strJointType_)
    {
        return !strJointType_.empty() && strJointType_ != "fixed";
    }

    std::pair<double, double> _DefaultMachineJointLimits(IN const std::string& strJointType_)
    {
        if (strJointType_ == "prismatic")
        {
            return { -1000.0, 1000.0 };
        }
        if (strJointType_ == "continuous")
        {
            return { -6.283185307179586, 6.283185307179586 };
        }
        return { -3.141592653589793, 3.141592653589793 };
    }

    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineJointComponent>>> _CollectMachineJointComponents(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineJointComponent>>> _Result;
        for (const auto& [_pEntity, _pElement] : _CollectEntitiesWithComponent<iCAX::CAM::CMachineElementComponent>(Repository_))
        {
            if (!_pEntity || !_pElement || _pElement->GetMachineID() != MachineID_)
            {
                continue;
            }

            auto _pJoint = _GetComponent<iCAX::CAM::CMachineJointComponent>(_pEntity);
            if (_pJoint)
            {
                _Result.emplace_back(_pEntity, _pJoint);
            }
        }

        std::sort(_Result.begin(), _Result.end(), [](IN const auto& Left_, IN const auto& Right_) {
            const auto _LeftElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(Left_.first);
            const auto _RightElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(Right_.first);
            const auto _LeftIndex = _LeftElement ? _LeftElement->GetSourceIndex() : 0ull;
            const auto _RightIndex = _RightElement ? _RightElement->GetSourceIndex() : 0ull;
            return _LeftIndex < _RightIndex;
        });
        return _Result;
    }

    std::shared_ptr<iCAX::CAM::CMachineKinematicsComponent> _GetOrAddMachineKinematics(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_)
    {
        return _GetOrAddEntityComponent<iCAX::CAM::CMachineKinematicsComponent>(pMachineEntity_);
    }

    std::shared_ptr<iCAX::CAM::CMachineStatusComponent> _GetOrAddMachineStatus(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_)
    {
        return _GetOrAddEntityComponent<iCAX::CAM::CMachineStatusComponent>(pMachineEntity_);
    }

    void _SetMachineDefaultKinematics(IN const std::shared_ptr<iCAX::CAM::CMachineKinematicsComponent>& pKinematics_)
    {
        _SetVariantArrayProperty(pKinematics_, iCAX::CAM::CMachineKinematicsComponent::PropertyName_TCP, _MakeDoubleArray({ 0.0, 0.0, 0.0 }));
        _SetVariantArrayProperty(pKinematics_, iCAX::CAM::CMachineKinematicsComponent::PropertyName_BeamDirection, _MakeDoubleArray({ 0.0, 0.0, 1.0 }));
        _SetDoubleProperty(pKinematics_, iCAX::CAM::CMachineKinematicsComponent::PropertyName_LinearJogStep, 10.0);
        _SetDoubleProperty(pKinematics_, iCAX::CAM::CMachineKinematicsComponent::PropertyName_AngularJogStep, 1.0);
    }

    std::string _UuidToOptionalString(IN const iCAX::Data::uuid& ID_)
    {
        return ID_.is_nil() ? std::string() : _UuidToString(ID_);
    }

    VariantArray _MakeUuidStringArray(IN const VariantArray& IDs_)
    {
        VariantArray _Result;
        _Result.reserve(IDs_.size());
        for (const auto& _Value : IDs_)
        {
            const auto _ID = iCAX::Transform::FromUuidVariant(_Value);
            if (!_ID.is_nil())
            {
                _Result.emplace_back(_UuidToString(_ID));
            }
        }
        return _Result;
    }

    VariantArray _MakeFilteredUuidStringArray(
        IN const VariantArray& IDs_,
        IN const std::unordered_set<std::string>& AllowedIDs_)
    {
        VariantArray _Result;
        _Result.reserve(IDs_.size());
        for (const auto& _Value : IDs_)
        {
            const auto _ID = iCAX::Transform::FromUuidVariant(_Value);
            if (_ID.is_nil())
            {
                continue;
            }

            const auto _IDText = _UuidToString(_ID);
            if (AllowedIDs_.find(_IDText) != AllowedIDs_.end())
            {
                _Result.emplace_back(_IDText);
            }
        }
        return _Result;
    }

    VariantArray _MakePublicMachineChildArray(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_,
        IN const std::unordered_set<std::string>& PublicEntityIDs_)
    {
        return pTransform_ ? _MakeFilteredUuidStringArray(pTransform_->GetChildEntityIDs(), PublicEntityIDs_) : VariantArray{};
    }

    ObjectMap _MakeTransformPayload(IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_)
    {
        ObjectMap _Transform;
        if (!pTransform_)
        {
            _Transform["hasTransform"] = false;
            return _Transform;
        }

        _Transform["hasTransform"] = true;
        _Transform["parentEntityId"] = _UuidToOptionalString(pTransform_->GetParentEntityID());
        _Transform["childEntityIds"] = _MakeUuidStringArray(pTransform_->GetChildEntityIDs());
        _Transform["position"] = _MakeDoubleArray({
            pTransform_->GetPositionX(),
            pTransform_->GetPositionY(),
            pTransform_->GetPositionZ()
        });
        _Transform["rotationRadians"] = _MakeDoubleArray({
            pTransform_->GetYawRadians(),
            pTransform_->GetPitchRadians(),
            pTransform_->GetRollRadians()
        });
        _Transform["scale"] = _MakeDoubleArray({
            pTransform_->GetScaleX(),
            pTransform_->GetScaleY(),
            pTransform_->GetScaleZ()
        });
        return _Transform;
    }

    VariantArray _MakeComponentNameArray(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        VariantArray _Components;
        if (!pEntity_)
        {
            return _Components;
        }

        const std::pair<const char*, const char*> _KnownComponents[] = {
            { "Machine", iCAX::CAM::CMachineInstanceComponent::S_ClassName },
            { "MachineElement", iCAX::CAM::CMachineElementComponent::S_ClassName },
            { "Transform", iCAX::Transform::CTransformComponent::S_ClassName },
            { "Link", iCAX::CAM::CMachineLinkComponent::S_ClassName },
            { "Joint", iCAX::CAM::CMachineJointComponent::S_ClassName },
            { "Tool", iCAX::CAM::CMachineToolComponent::S_ClassName },
            { "Visual", iCAX::CAM::CMachineVisualComponent::S_ClassName },
            { "Collision", iCAX::CAM::CMachineCollisionComponent::S_ClassName },
            { "Appearance", iCAX::CAM::CMachineAppearanceComponent::S_ClassName },
            { "Render", iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName },
        };
        for (const auto& [_strName, _strComponentClass] : _KnownComponents)
        {
            if (pEntity_->HasComponent(_strComponentClass))
            {
                _Components.emplace_back(std::string(_strName));
            }
        }
        return _Components;
    }

    bool _HasMachineVisualAttachments(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        auto _pVisuals = _GetComponent<iCAX::CAM::CMachineVisualComponent>(pEntity_);
        return _pVisuals && !_pVisuals->GetVisuals().empty();
    }

    bool _HasMachineCollisionAttachments(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        auto _pCollisions = _GetComponent<iCAX::CAM::CMachineCollisionComponent>(pEntity_);
        return _pCollisions && !_pCollisions->GetCollisions().empty();
    }

    ObjectMap _MakeAttachmentListItem(
        IN const ObjectMap& Attachment_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pOwnerEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineElementComponent>& pOwnerElement_,
        IN const std::string& strAttachmentKind_)
    {
        auto _Item = Attachment_;
        _Item["attachmentKind"] = strAttachmentKind_;
        _Item["entityId"] = pOwnerEntity_ ? _UuidToString(pOwnerEntity_->GetID()) : std::string();
        _Item["machineId"] = pOwnerElement_ ? _UuidToString(pOwnerElement_->GetMachineID()) : std::string();
        _Item["ownerElementKind"] = pOwnerElement_ ? pOwnerElement_->GetElementKind() : std::string();
        _Item["ownerElementName"] = pOwnerElement_ ? pOwnerElement_->GetName() : std::string();
        _Item["sourceIndex"] = pOwnerElement_ ? pOwnerElement_->GetSourceIndex() : 0ull;
        return _Item;
    }

    ObjectMap _MakeMachineElementPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineElementComponent>& pElement_,
        IN const VariantArray* pPublicChildEntityIDs_ = nullptr)
    {
        const auto _pTransform = _GetComponent<iCAX::Transform::CTransformComponent>(pEntity_);

        ObjectMap _Element;
        _Element["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Element["machineId"] = pElement_ ? _UuidToString(pElement_->GetMachineID()) : std::string();
        _Element["parentEntityId"] = _pTransform ? _UuidToOptionalString(_pTransform->GetParentEntityID()) : std::string();
        _Element["childEntityIds"] = pPublicChildEntityIDs_
            ? *pPublicChildEntityIDs_
            : (_pTransform ? _MakeUuidStringArray(_pTransform->GetChildEntityIDs()) : VariantArray{});
        _Element["kind"] = pElement_ ? pElement_->GetElementKind() : std::string();
        _Element["name"] = pElement_ ? pElement_->GetName() : std::string();
        _Element["sourceIndex"] = pElement_ ? pElement_->GetSourceIndex() : 0ull;
        _Element["hasTransform"] = _pTransform != nullptr;
        _Element["hasLink"] = _GetComponent<iCAX::CAM::CMachineLinkComponent>(pEntity_) != nullptr;
        _Element["hasJoint"] = _GetComponent<iCAX::CAM::CMachineJointComponent>(pEntity_) != nullptr;
        _Element["hasTool"] = _GetComponent<iCAX::CAM::CMachineToolComponent>(pEntity_) != nullptr;
        _Element["hasVisual"] = _HasMachineVisualAttachments(pEntity_);
        _Element["hasCollision"] = _HasMachineCollisionAttachments(pEntity_);
        _Element["hasRender"] = _GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(pEntity_) != nullptr;
        return _Element;
    }

    ObjectMap _MakeMachineRootElementPayload(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_,
        IN const VariantArray* pPublicChildEntityIDs_ = nullptr)
    {
        const auto _pTransform = _GetComponent<iCAX::Transform::CTransformComponent>(pEntity_);

        ObjectMap _Element;
        _Element["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Element["machineId"] = _Element["entityId"];
        _Element["parentEntityId"] = _pTransform ? _UuidToOptionalString(_pTransform->GetParentEntityID()) : std::string();
        _Element["childEntityIds"] = pPublicChildEntityIDs_
            ? *pPublicChildEntityIDs_
            : (_pTransform ? _MakeUuidStringArray(_pTransform->GetChildEntityIDs()) : VariantArray{});
        _Element["kind"] = std::string("machine");
        _Element["name"] = pMachine_ ? pMachine_->GetName() : std::string("Machine");
        _Element["sourceIndex"] = 0ull;
        _Element["hasTransform"] = _pTransform != nullptr;
        _Element["hasLink"] = false;
        _Element["hasJoint"] = false;
        _Element["hasTool"] = false;
        _Element["hasVisual"] = false;
        _Element["hasCollision"] = false;
        _Element["hasRender"] = _GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(pEntity_) != nullptr;
        return _Element;
    }

    template <typename TComponent>
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _CollectMachineChildComponents(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
        for (const auto& [_pEntity, _pComponent] : _CollectEntitiesWithComponent<TComponent>(Repository_))
        {
            auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
            if (_pEntity && _pComponent && _pElement && _pElement->GetMachineID() == MachineID_)
            {
                _Result.emplace_back(_pEntity, _pComponent);
            }
        }

        std::sort(_Result.begin(), _Result.end(), [](IN const auto& Left_, IN const auto& Right_) {
            const auto _pLeftElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(Left_.first);
            const auto _pRightElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(Right_.first);
            const auto _LeftIndex = _pLeftElement ? _pLeftElement->GetSourceIndex() : 0ull;
            const auto _RightIndex = _pRightElement ? _pRightElement->GetSourceIndex() : 0ull;
            return _LeftIndex < _RightIndex;
        });
        return _Result;
    }

    VariantArray _MakeMachineLinkArray(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        VariantArray _Links;
        for (const auto& [_pEntity, _pLink] : _CollectMachineChildComponents<iCAX::CAM::CMachineLinkComponent>(Repository_, MachineID_))
        {
            auto _Link = _MakeMachineElementPayload(_pEntity, _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity));
            _Link["linkName"] = _pLink->GetLinkName();
            _Link["selfCollide"] = _pLink->GetSelfCollide();
            _Link["gravity"] = _pLink->GetGravity();
            _Link["kinematic"] = _pLink->GetKinematic();
            if (auto _pVisuals = _GetComponent<iCAX::CAM::CMachineVisualComponent>(_pEntity))
            {
                _Link["visuals"] = _pVisuals->GetVisuals();
            }
            if (auto _pCollisions = _GetComponent<iCAX::CAM::CMachineCollisionComponent>(_pEntity))
            {
                _Link["collisions"] = _pCollisions->GetCollisions();
            }
            _Links.emplace_back(_Link);
        }
        return _Links;
    }

    VariantArray _MakeMachineJointArray(
        IN const std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineJointComponent>>>& Joints_)
    {
        VariantArray _Joints;
        for (const auto& [_pEntity, _pJoint] : Joints_)
        {
            auto _Joint = _MakeMachineElementPayload(_pEntity, _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity));
            _Joint["kind"] = std::string("joint");
            _Joint["jointName"] = _pJoint->GetJointName();
            _Joint["type"] = _pJoint->GetJointType();
            _Joint["parent"] = _pJoint->GetParentLinkName();
            _Joint["child"] = _pJoint->GetChildLinkName();
            _Joint["axis"] = _pJoint->GetAxis();
            _Joint["axis2"] = _pJoint->GetAxis2();
            _Joint["lower"] = _pJoint->GetLowerLimit();
            _Joint["upper"] = _pJoint->GetUpperLimit();
            _Joint["position"] = _pJoint->GetPosition();
            _Joints.emplace_back(_Joint);
        }
        return _Joints;
    }

    ObjectMap _MakeToolPayload(
        IN const std::shared_ptr<iCAX::CAM::CMachineToolComponent>& pTool_,
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_);

    VariantArray _MakeMachineToolArray(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        VariantArray _Tools;
        for (const auto& [_pEntity, _pTool] : _CollectMachineChildComponents<iCAX::CAM::CMachineToolComponent>(Repository_, MachineID_))
        {
            auto _Tool = _MakeMachineElementPayload(_pEntity, _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity));
            _Tool["tool"] = _MakeToolPayload(_pTool, _GetComponent<iCAX::Transform::CTransformComponent>(_pEntity));
            _Tools.emplace_back(_Tool);
        }
        return _Tools;
    }

    VariantArray _MakeMachineVisualArray(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        VariantArray _Visuals;
        for (const auto& [_pEntity, _pVisual] : _CollectMachineChildComponents<iCAX::CAM::CMachineVisualComponent>(Repository_, MachineID_))
        {
            const auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
            for (const auto& _VisualVariant : _pVisual->GetVisuals())
            {
                if (!_VisualVariant.Is<ObjectMap>())
                {
                    continue;
                }
                _Visuals.emplace_back(_MakeAttachmentListItem(_VisualVariant.To<ObjectMap>(), _pEntity, _pElement, "visual"));
            }
        }
        return _Visuals;
    }

    ObjectMap _MakeMachineMaterialPayload(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const ObjectMap& Visual_)
    {
        ObjectMap _Material;
        const auto _MaterialResourceID = _GetObjectString(Visual_, "materialResourceId");
        _Material["entityId"] = Visual_.at("entityId");
        _Material["visualName"] = Visual_.at("visualName");
        _Material["linkName"] = Visual_.at("linkName");
        _Material["materialResourceId"] = _MaterialResourceID;

        auto _pMaterial = Resources_.Get<iCAX::Render::SRenderMaterialData>(_MaterialResourceID);
        if (_pMaterial)
        {
            _Material["colorRgba"] = static_cast<unsigned long long>(_pMaterial->nColorRGBA);
            _Material["ambientRgba"] = static_cast<unsigned long long>(_pMaterial->nAmbientRGBA);
            _Material["specularRgba"] = static_cast<unsigned long long>(_pMaterial->nSpecularRGBA);
            _Material["emissiveRgba"] = static_cast<unsigned long long>(_pMaterial->nEmissiveRGBA);
            _Material["lineWidth"] = static_cast<double>(_pMaterial->nLineWidth);
            _Material["materialFlags"] = static_cast<unsigned long long>(_pMaterial->nMaterialFlags);
            _Material["lighting"] = (_pMaterial->nMaterialFlags & iCAX::Render::kRenderStyleFlagLightingDisabled) == 0;
            _Material["version"] = static_cast<unsigned long long>(_pMaterial->nDataVersion);
        }
        return _Material;
    }

    VariantArray _MakeMachineMaterialArray(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const VariantArray& Visuals_)
    {
        VariantArray _Materials;
        for (const auto& _VisualVariant : Visuals_)
        {
            if (!_VisualVariant.Is<ObjectMap>())
            {
                continue;
            }

            const auto _Visual = _VisualVariant.To<ObjectMap>();
            _Materials.emplace_back(_MakeMachineMaterialPayload(Resources_, _Visual));
        }
        return _Materials;
    }

    VariantArray _MakeMachineCollisionArray(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        VariantArray _Collisions;
        for (const auto& [_pEntity, _pCollision] : _CollectMachineChildComponents<iCAX::CAM::CMachineCollisionComponent>(Repository_, MachineID_))
        {
            const auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
            for (const auto& _CollisionVariant : _pCollision->GetCollisions())
            {
                if (!_CollisionVariant.Is<ObjectMap>())
                {
                    continue;
                }
                _Collisions.emplace_back(_MakeAttachmentListItem(_CollisionVariant.To<ObjectMap>(), _pEntity, _pElement, "collision"));
            }
        }
        return _Collisions;
    }

    VariantArray _MakeMachineElementArray(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_)
    {
        VariantArray _Elements;
        if (!pMachineEntity_ || !pMachine_)
        {
            return _Elements;
        }

        const auto _MachineID = pMachineEntity_->GetID();
        auto _Children = _CollectEntitiesWithComponent<iCAX::CAM::CMachineElementComponent>(Repository_);
        _Children.erase(std::remove_if(_Children.begin(), _Children.end(), [&_MachineID](IN const auto& Item_) {
            const auto& [_pEntity, _pElement] = Item_;
            return !_pEntity
                || !_pElement
                || _pElement->GetMachineID() != _MachineID
                || _pElement->GetElementKind() != "part";
        }), _Children.end());

        std::sort(_Children.begin(), _Children.end(), [](IN const auto& Left_, IN const auto& Right_) {
            const auto _LeftIndex = Left_.second ? Left_.second->GetSourceIndex() : 0ull;
            const auto _RightIndex = Right_.second ? Right_.second->GetSourceIndex() : 0ull;
            return _LeftIndex < _RightIndex;
        });

        std::unordered_set<std::string> _PublicEntityIDs;
        _PublicEntityIDs.insert(_UuidToString(_MachineID));
        for (const auto& [_pEntity, _pElement] : _Children)
        {
            (void)_pElement;
            _PublicEntityIDs.insert(_UuidToString(_pEntity->GetID()));
        }

        const auto _RootChildren = _MakePublicMachineChildArray(
            _GetComponent<iCAX::Transform::CTransformComponent>(pMachineEntity_),
            _PublicEntityIDs);
        _Elements.emplace_back(_MakeMachineRootElementPayload(pMachineEntity_, pMachine_, &_RootChildren));

        for (const auto& [_pEntity, _pElement] : _Children)
        {
            const auto _ElementChildren = _MakePublicMachineChildArray(
                _GetComponent<iCAX::Transform::CTransformComponent>(_pEntity),
                _PublicEntityIDs);
            _Elements.emplace_back(_MakeMachineElementPayload(_pEntity, _pElement, &_ElementChildren));
        }
        return _Elements;
    }

    ObjectMap _MakeMachineComponentPayload(
        IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_,
        IN const std::shared_ptr<iCAX::CAM::CMachineStatusComponent>& pStatus_,
        IN const std::shared_ptr<iCAX::CAM::CMachineKinematicsComponent>& pKinematics_)
    {
        ObjectMap _Machine;
        if (!pMachine_)
        {
            return _Machine;
        }

        _Machine["name"] = pMachine_->GetName();
        _Machine["modelName"] = pMachine_->GetModelName();
        _Machine["workstationName"] = pMachine_->GetWorkstationName();
        _Machine["descriptionFormat"] = pMachine_->GetDescriptionFormat();
        _Machine["descriptionVersion"] = pMachine_->GetDescriptionVersion();
        _Machine["enabled"] = pMachine_->GetEnabled();
        _Machine["status"] = pStatus_ ? pStatus_->GetStatus() : std::string();
        _Machine["lastCheckResult"] = pStatus_ ? pStatus_->GetLastCheckResult() : std::string();
        _Machine["tcp"] = pKinematics_ ? pKinematics_->GetTCP() : VariantArray{};
        _Machine["beamDirection"] = pKinematics_ ? pKinematics_->GetBeamDirection() : VariantArray{};
        _Machine["maxVelocity"] = pKinematics_ ? pKinematics_->GetMaxVelocity() : 0.0;
        _Machine["maxAcceleration"] = pKinematics_ ? pKinematics_->GetMaxAcceleration() : 0.0;
        _Machine["linearJogStep"] = pKinematics_ ? pKinematics_->GetLinearJogStep() : 10.0;
        _Machine["angularJogStep"] = pKinematics_ ? pKinematics_->GetAngularJogStep() : 1.0;
        return _Machine;
    }

    ObjectMap _MakeLinkPayload(IN const std::shared_ptr<iCAX::CAM::CMachineLinkComponent>& pLink_)
    {
        ObjectMap _Link;
        if (!pLink_)
        {
            return _Link;
        }

        _Link["name"] = pLink_->GetLinkName();
        _Link["selfCollide"] = pLink_->GetSelfCollide();
        _Link["gravity"] = pLink_->GetGravity();
        _Link["kinematic"] = pLink_->GetKinematic();
        return _Link;
    }

    ObjectMap _MakeJointPayload(IN const std::shared_ptr<iCAX::CAM::CMachineJointComponent>& pJoint_)
    {
        ObjectMap _Joint;
        if (!pJoint_)
        {
            return _Joint;
        }

        _Joint["name"] = pJoint_->GetJointName();
        _Joint["type"] = pJoint_->GetJointType();
        _Joint["parentLink"] = pJoint_->GetParentLinkName();
        _Joint["childLink"] = pJoint_->GetChildLinkName();
        _Joint["axis"] = pJoint_->GetAxis();
        _Joint["axis2"] = pJoint_->GetAxis2();
        _Joint["lower"] = pJoint_->GetLowerLimit();
        _Joint["upper"] = pJoint_->GetUpperLimit();
        _Joint["position"] = pJoint_->GetPosition();
        return _Joint;
    }

    ObjectMap _MakeVisualPayload(IN const std::shared_ptr<iCAX::CAM::CMachineVisualComponent>& pVisual_)
    {
        ObjectMap _Visual;
        if (!pVisual_)
        {
            return _Visual;
        }

        _Visual["count"] = static_cast<unsigned long long>(pVisual_->GetVisuals().size());
        _Visual["items"] = pVisual_->GetVisuals();
        return _Visual;
    }

    ObjectMap _MakeCollisionPayload(IN const std::shared_ptr<iCAX::CAM::CMachineCollisionComponent>& pCollision_)
    {
        ObjectMap _Collision;
        if (!pCollision_)
        {
            return _Collision;
        }

        _Collision["count"] = static_cast<unsigned long long>(pCollision_->GetCollisions().size());
        _Collision["items"] = pCollision_->GetCollisions();
        return _Collision;
    }

    std::string _ColorToHex(IN unsigned long long nColorRGBA_)
    {
        std::ostringstream _Stream;
        _Stream << "#" << std::uppercase << std::hex << std::setfill('0') << std::setw(8)
            << static_cast<uint32_t>(nColorRGBA_ & 0xFFFFFFFFull);
        return _Stream.str();
    }

    ObjectMap _MakeAppearancePayload(IN const std::shared_ptr<iCAX::CAM::CMachineAppearanceComponent>& pAppearance_)
    {
        const auto _UseOverride = pAppearance_ ? pAppearance_->GetUseColorOverride() : false;
        const auto _Color = pAppearance_ ? pAppearance_->GetColorRGBA() : 0x8FB8C9FFull;
        ObjectMap _Appearance;
        _Appearance["useColorOverride"] = _UseOverride;
        _Appearance["colorRGBA"] = _Color;
        _Appearance["colorHex"] = _ColorToHex(_Color);
        _Appearance["showCollision"] = pAppearance_ ? pAppearance_->GetShowCollision() : true;
        return _Appearance;
    }

    std::vector<double> _ReadVector3OrDefault(IN const VariantArray& Values_, IN const std::vector<double>& Default_)
    {
        if (Values_.size() != 3)
        {
            auto _Result = Default_;
            _Result.resize(3, 0.0);
            return _Result;
        }
        return _VariantArrayToDoubles(Values_, "machineTool.vector3");
    }

    std::vector<double> _NormalizeVector3(IN std::vector<double> Values_, IN const std::vector<double>& Default_)
    {
        Values_.resize(3, 0.0);
        const auto _Length = std::sqrt(
            Values_[0] * Values_[0]
            + Values_[1] * Values_[1]
            + Values_[2] * Values_[2]);
        if (_Length <= 0.0)
        {
            auto _Result = Default_;
            _Result.resize(3, 0.0);
            return _Result;
        }
        return { Values_[0] / _Length, Values_[1] / _Length, Values_[2] / _Length };
    }

    std::vector<double> _TransformPoint(
        IN const iCAX::Data::Double4x4& Matrix_,
        IN const std::vector<double>& Point_)
    {
        return {
            Matrix_(0, 0) * Point_[0] + Matrix_(0, 1) * Point_[1] + Matrix_(0, 2) * Point_[2] + Matrix_(0, 3),
            Matrix_(1, 0) * Point_[0] + Matrix_(1, 1) * Point_[1] + Matrix_(1, 2) * Point_[2] + Matrix_(1, 3),
            Matrix_(2, 0) * Point_[0] + Matrix_(2, 1) * Point_[1] + Matrix_(2, 2) * Point_[2] + Matrix_(2, 3)
        };
    }

    std::vector<double> _TransformDirection(
        IN const iCAX::Data::Double4x4& Matrix_,
        IN const std::vector<double>& Direction_)
    {
        return _NormalizeVector3({
            Matrix_(0, 0) * Direction_[0] + Matrix_(0, 1) * Direction_[1] + Matrix_(0, 2) * Direction_[2],
            Matrix_(1, 0) * Direction_[0] + Matrix_(1, 1) * Direction_[1] + Matrix_(1, 2) * Direction_[2],
            Matrix_(2, 0) * Direction_[0] + Matrix_(2, 1) * Direction_[1] + Matrix_(2, 2) * Direction_[2]
        }, { 0.0, 0.0, -1.0 });
    }

    ObjectMap _MakeToolPayload(
        IN const std::shared_ptr<iCAX::CAM::CMachineToolComponent>& pTool_,
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_)
    {
        ObjectMap _Tool;
        if (!pTool_)
        {
            return _Tool;
        }

        const auto _TcpLocal = _ReadVector3OrDefault(pTool_->GetTcpLocalPosition(), { 0.0, 0.0, 0.0 });
        const auto _BeamLocal = _NormalizeVector3(
            _ReadVector3OrDefault(pTool_->GetBeamLocalDirection(), { 0.0, 0.0, -1.0 }),
            { 0.0, 0.0, -1.0 });

        _Tool["name"] = pTool_->GetToolName();
        _Tool["type"] = pTool_->GetToolType();
        _Tool["tcpLocalPosition"] = _DoublesToVariantArray(_TcpLocal);
        _Tool["beamLocalDirection"] = _DoublesToVariantArray(_BeamLocal);
        if (pTransform_)
        {
            const auto _LocalToWorld = pTransform_->GetLocalToWorldMatrix();
            _Tool["tcpWorldPosition"] = _DoublesToVariantArray(_TransformPoint(_LocalToWorld, _TcpLocal));
            _Tool["beamWorldDirection"] = _DoublesToVariantArray(_TransformDirection(_LocalToWorld, _BeamLocal));
        }
        return _Tool;
    }

    ObjectMap _MakeMachinePayload(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_)
    {
        const auto _MachineID = pEntity_ ? pEntity_->GetID() : iCAX::Data::uuid();
        const auto _pKinematics = _GetComponent<iCAX::CAM::CMachineKinematicsComponent>(pEntity_);
        const auto _pStatus = _GetComponent<iCAX::CAM::CMachineStatusComponent>(pEntity_);
        const auto _Joints = pEntity_
            ? _CollectMachineJointComponents(Repository_, _MachineID)
            : std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineJointComponent>>>();

        VariantArray _JointNames;
        VariantArray _JointPositions;
        VariantArray _JointLowerLimits;
        VariantArray _JointUpperLimits;
        _JointNames.reserve(_Joints.size());
        _JointPositions.reserve(_Joints.size());
        _JointLowerLimits.reserve(_Joints.size());
        _JointUpperLimits.reserve(_Joints.size());
        for (const auto& [_pJointEntity, _pJoint] : _Joints)
        {
            (void)_pJointEntity;
            if (!_pJoint)
            {
                continue;
            }
            _JointNames.emplace_back(_pJoint->GetJointName());
            _JointPositions.emplace_back(_pJoint->GetPosition());
            _JointLowerLimits.emplace_back(_pJoint->GetLowerLimit());
            _JointUpperLimits.emplace_back(_pJoint->GetUpperLimit());
        }

        ObjectMap _Machine;
        _Machine["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Machine["id"] = _Machine["entityId"];
        _Machine["name"] = pMachine_ ? pMachine_->GetName() : std::string();
        _Machine["sourcePath"] = pMachine_ ? pMachine_->GetSourcePath() : std::string();
        _Machine["machineDefinitionId"] = pMachine_ ? pMachine_->GetMachineDefinitionID() : std::string();
        _Machine["machineResourceId"] = pMachine_ ? pMachine_->GetMachineResourceID() : std::string();
        _Machine["descriptionFormat"] = pMachine_ ? pMachine_->GetDescriptionFormat() : std::string();
        _Machine["descriptionVersion"] = pMachine_ ? pMachine_->GetDescriptionVersion() : std::string();
        _Machine["modelName"] = pMachine_ ? pMachine_->GetModelName() : std::string();
        _Machine["workstationName"] = pMachine_ ? pMachine_->GetWorkstationName() : std::string("默认工位");
        _Machine["staticModel"] = pMachine_ ? pMachine_->GetStaticModel() : false;
        _Machine["enabled"] = pMachine_ ? pMachine_->GetEnabled() : false;
        _Machine["status"] = _pStatus ? _pStatus->GetStatus() : std::string("NotLoaded");
        _Machine["jointNames"] = _JointNames;
        _Machine["jointPositions"] = _JointPositions;
        _Machine["jointLowerLimits"] = _JointLowerLimits;
        _Machine["jointUpperLimits"] = _JointUpperLimits;
        _Machine["tcp"] = _pKinematics ? _pKinematics->GetTCP() : VariantArray{};
        _Machine["beamDirection"] = _pKinematics ? _pKinematics->GetBeamDirection() : VariantArray{};
        _Machine["maxVelocity"] = _pKinematics ? _pKinematics->GetMaxVelocity() : 0.0;
        _Machine["maxAcceleration"] = _pKinematics ? _pKinematics->GetMaxAcceleration() : 0.0;
        _Machine["linearJogStep"] = _pKinematics ? _pKinematics->GetLinearJogStep() : 10.0;
        _Machine["angularJogStep"] = _pKinematics ? _pKinematics->GetAngularJogStep() : 1.0;
        const auto _Visuals = pEntity_ ? _MakeMachineVisualArray(Repository_, _MachineID) : VariantArray{};
        _Machine["links"] = pEntity_ ? _MakeMachineLinkArray(Repository_, _MachineID) : VariantArray{};
        _Machine["joints"] = _MakeMachineJointArray(_Joints);
        _Machine["tools"] = pEntity_ ? _MakeMachineToolArray(Repository_, _MachineID) : VariantArray{};
        _Machine["visuals"] = _Visuals;
        _Machine["collisions"] = pEntity_ ? _MakeMachineCollisionArray(Repository_, _MachineID) : VariantArray{};
        _Machine["elements"] = pEntity_ ? _MakeMachineElementArray(Repository_, pEntity_, pMachine_) : VariantArray{};
        _Machine["materials"] = _MakeMachineMaterialArray(Resources_, _Visuals);
        _Machine["includes"] = VariantArray{};
        _Machine["frames"] = VariantArray{};
        _Machine["plugins"] = VariantArray{};
        _Machine["lastCheckResult"] = _pStatus ? _pStatus->GetLastCheckResult() : std::string();
        _Machine["isLoaded"] = pMachine_ && !pMachine_->GetSourcePath().empty();
        return _Machine;
    }

    ObjectMap _MakeMachineElementDetailPayload(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& EntityID_)
    {
        auto _pEntity = Repository_.GetEntity(EntityID_);
        if (!_pEntity)
        {
            throw std::invalid_argument("Cam Machine.GetElement entity does not exist: " + _UuidToString(EntityID_));
        }

        auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pEntity);
        auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
        if (!_pMachine && !_pElement)
        {
            throw std::invalid_argument("Cam Machine.GetElement entity is not a machine element: " + _UuidToString(EntityID_));
        }

        ObjectMap _Detail = _pMachine
            ? _MakeMachineRootElementPayload(_pEntity, _pMachine)
            : _MakeMachineElementPayload(_pEntity, _pElement);

        _Detail["components"] = _MakeComponentNameArray(_pEntity);
        _Detail["transform"] = _MakeTransformPayload(_GetComponent<iCAX::Transform::CTransformComponent>(_pEntity));
        _Detail["transformEditPolicy"] = iCAX::CAM::MakeMachineTransformEditPolicyPayload(*_pEntity);
        _Detail["appearance"] = _MakeAppearancePayload(_GetComponent<iCAX::CAM::CMachineAppearanceComponent>(_pEntity));

        if (_pMachine)
        {
            _Detail["machine"] = _MakeMachineComponentPayload(
                _pMachine,
                _GetComponent<iCAX::CAM::CMachineStatusComponent>(_pEntity),
                _GetComponent<iCAX::CAM::CMachineKinematicsComponent>(_pEntity));
        }

        auto _pLink = _GetComponent<iCAX::CAM::CMachineLinkComponent>(_pEntity);
        if (_pLink)
        {
            _Detail["link"] = _MakeLinkPayload(_pLink);
        }

        auto _pJoint = _GetComponent<iCAX::CAM::CMachineJointComponent>(_pEntity);
        if (_pJoint)
        {
            _Detail["joint"] = _MakeJointPayload(_pJoint);
        }

        auto _pTool = _GetComponent<iCAX::CAM::CMachineToolComponent>(_pEntity);
        if (_pTool)
        {
            _Detail["tool"] = _MakeToolPayload(_pTool, _GetComponent<iCAX::Transform::CTransformComponent>(_pEntity));
        }

        auto _pVisual = _GetComponent<iCAX::CAM::CMachineVisualComponent>(_pEntity);
        if (_pVisual)
        {
            _Detail["visual"] = _MakeVisualPayload(_pVisual);
            _Detail["visuals"] = _pVisual->GetVisuals();
        }

        auto _pCollision = _GetComponent<iCAX::CAM::CMachineCollisionComponent>(_pEntity);
        if (_pCollision)
        {
            _Detail["collision"] = _MakeCollisionPayload(_pCollision);
            _Detail["collisions"] = _pCollision->GetCollisions();
        }

        return _Detail;
    }

    VariantArray _MakeMachineArray(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Machines;
        for (const auto& [_pEntity, _pMachine] : _CollectEntitiesWithComponent<iCAX::CAM::CMachineInstanceComponent>(Repository_))
        {
            _Machines.emplace_back(_MakeMachinePayload(Resources_, Repository_, _pEntity, _pMachine));
        }
        return _Machines;
    }

    std::optional<ObjectMap> _TryObjectMap(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }
        return Value_.To<ObjectMap>();
    }

    std::vector<double> _ReadObjectDoubleArray(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN size_t nCount_,
        IN const std::vector<double>& Default_)
    {
        auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || !_Iter->second.Is<VariantArray>())
        {
            auto _Result = Default_;
            _Result.resize(nCount_, 0.0);
            return _Result;
        }

        auto _Values = _VariantArrayToDoubles(_Iter->second.To<VariantArray>(), strName_);
        auto _Result = Default_;
        _Result.resize(nCount_, 0.0);
        for (size_t _Index = 0; _Index < nCount_ && _Index < _Values.size(); ++_Index)
        {
            _Result[_Index] = _Values[_Index];
        }
        return _Result;
    }

    std::array<double, 6> _ReadPoseArray(IN const ObjectMap& Object_)
    {
        auto _Values = _ReadObjectDoubleArray(Object_, "pose", 6, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 });
        return { _Values[0], _Values[1], _Values[2], _Values[3], _Values[4], _Values[5] };
    }

    void _AppendBoxMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dSizeX_,
        IN double dSizeY_,
        IN double dSizeZ_)
    {
        const float _X = static_cast<float>(std::max(0.001, std::abs(dSizeX_)) * 0.5);
        const float _Y = static_cast<float>(std::max(0.001, std::abs(dSizeY_)) * 0.5);
        const float _Z = static_cast<float>(std::max(0.001, std::abs(dSizeZ_)) * 0.5);
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.insert(Mesh_.Positions.end(), {
            { -_X, -_Y, -_Z }, { _X, -_Y, -_Z }, { _X, _Y, -_Z }, { -_X, _Y, -_Z },
            { -_X, -_Y, _Z }, { _X, -_Y, _Z }, { _X, _Y, _Z }, { -_X, _Y, _Z }
        });
        const uint32_t _Indices[] = {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            0, 4, 5, 0, 5, 1,
            1, 5, 6, 1, 6, 2,
            2, 6, 7, 2, 7, 3,
            3, 7, 4, 3, 4, 0
        };
        for (const auto _Index : _Indices)
        {
            Mesh_.Indices.push_back(_Base + _Index);
        }
    }

    void _AppendPlaneMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dSizeX_,
        IN double dSizeY_)
    {
        const float _X = static_cast<float>(std::max(0.001, std::abs(dSizeX_)) * 0.5);
        const float _Y = static_cast<float>(std::max(0.001, std::abs(dSizeY_)) * 0.5);
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.insert(Mesh_.Positions.end(), {
            { -_X, -_Y, 0.0f }, { _X, -_Y, 0.0f }, { _X, _Y, 0.0f }, { -_X, _Y, 0.0f }
        });
        Mesh_.Indices.insert(Mesh_.Indices.end(), { _Base, _Base + 1, _Base + 2, _Base, _Base + 2, _Base + 3 });
    }

    void _AppendCylinderMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dRadius_,
        IN double dLength_)
    {
        constexpr uint32_t _Segments = 32;
        const float _Radius = static_cast<float>(std::max(0.001, std::abs(dRadius_)));
        const float _HalfLength = static_cast<float>(std::max(0.001, std::abs(dLength_)) * 0.5);
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        for (uint32_t _Index = 0; _Index < _Segments; ++_Index)
        {
            const float _Angle = static_cast<float>((6.283185307179586 * _Index) / _Segments);
            Mesh_.Positions.push_back({ std::cos(_Angle) * _Radius, std::sin(_Angle) * _Radius, -_HalfLength });
            Mesh_.Positions.push_back({ std::cos(_Angle) * _Radius, std::sin(_Angle) * _Radius, _HalfLength });
        }

        const uint32_t _BottomCenter = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.push_back({ 0.0f, 0.0f, -_HalfLength });
        const uint32_t _TopCenter = static_cast<uint32_t>(Mesh_.Positions.size());
        Mesh_.Positions.push_back({ 0.0f, 0.0f, _HalfLength });

        for (uint32_t _Index = 0; _Index < _Segments; ++_Index)
        {
            const uint32_t _Next = (_Index + 1) % _Segments;
            const uint32_t _B0 = _Base + _Index * 2;
            const uint32_t _T0 = _B0 + 1;
            const uint32_t _B1 = _Base + _Next * 2;
            const uint32_t _T1 = _B1 + 1;
            Mesh_.Indices.insert(Mesh_.Indices.end(), { _B0, _B1, _T1, _B0, _T1, _T0 });
            Mesh_.Indices.insert(Mesh_.Indices.end(), { _BottomCenter, _B0, _B1 });
            Mesh_.Indices.insert(Mesh_.Indices.end(), { _TopCenter, _T1, _T0 });
        }
    }

    void _AppendSphereMesh(
        IN OUT iCAX::Render::SRenderMeshData& Mesh_,
        IN double dRadius_)
    {
        constexpr uint32_t _Latitudes = 12;
        constexpr uint32_t _Longitudes = 24;
        const float _Radius = static_cast<float>(std::max(0.001, std::abs(dRadius_)));
        const uint32_t _Base = static_cast<uint32_t>(Mesh_.Positions.size());
        for (uint32_t _Lat = 0; _Lat <= _Latitudes; ++_Lat)
        {
            const float _Theta = static_cast<float>((3.141592653589793 * _Lat) / _Latitudes);
            const float _SinTheta = std::sin(_Theta);
            const float _CosTheta = std::cos(_Theta);
            for (uint32_t _Lon = 0; _Lon <= _Longitudes; ++_Lon)
            {
                const float _Phi = static_cast<float>((6.283185307179586 * _Lon) / _Longitudes);
                Mesh_.Positions.push_back({
                    std::cos(_Phi) * _SinTheta * _Radius,
                    std::sin(_Phi) * _SinTheta * _Radius,
                    _CosTheta * _Radius
                });
            }
        }

        for (uint32_t _Lat = 0; _Lat < _Latitudes; ++_Lat)
        {
            for (uint32_t _Lon = 0; _Lon < _Longitudes; ++_Lon)
            {
                const uint32_t _A = _Base + _Lat * (_Longitudes + 1) + _Lon;
                const uint32_t _B = _A + _Longitudes + 1;
                Mesh_.Indices.insert(Mesh_.Indices.end(), { _A, _B, _A + 1, _B, _B + 1, _A + 1 });
            }
        }
    }

    bool _IsMachineExternalMeshGeometry(IN const ObjectMap& Source_)
    {
        const auto _Geometry = _TryGetObjectMapField(Source_, "geometry");
        return _Geometry && _GetObjectString(*_Geometry, "type") == "mesh";
    }

    iCAX::Render::SRenderMeshData _MakeMachinePrimitiveRenderMesh(IN const ObjectMap& Visual_)
    {
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;

        const auto _Geometry = _TryGetObjectMapField(Visual_, "geometry");
        const auto _Type = _Geometry ? _GetObjectString(*_Geometry, "type") : std::string();
        if (_Type == "box")
        {
            const auto _Size = _ReadObjectDoubleArray(*_Geometry, "size", 3, { 1.0, 1.0, 1.0 });
            _AppendBoxMesh(_Mesh, _Size[0] * kMachineDescriptionLengthToWorld, _Size[1] * kMachineDescriptionLengthToWorld, _Size[2] * kMachineDescriptionLengthToWorld);
        }
        else if (_Type == "cylinder")
        {
            _AppendCylinderMesh(
                _Mesh,
                _GetObjectDoubleOrDefault(*_Geometry, "radius", 0.5) * kMachineDescriptionLengthToWorld,
                _GetObjectDoubleOrDefault(*_Geometry, "length", 1.0) * kMachineDescriptionLengthToWorld);
        }
        else if (_Type == "sphere")
        {
            _AppendSphereMesh(_Mesh, _GetObjectDoubleOrDefault(*_Geometry, "radius", 0.5) * kMachineDescriptionLengthToWorld);
        }
        else if (_Type == "plane")
        {
            const auto _Size = _ReadObjectDoubleArray(*_Geometry, "size", 2, { 1.0, 1.0 });
            _AppendPlaneMesh(_Mesh, _Size[0] * kMachineDescriptionLengthToWorld, _Size[1] * kMachineDescriptionLengthToWorld);
        }
        else if (_Type == "mesh")
        {
            throw std::invalid_argument("Machine mesh geometry must be imported as triangle mesh resource");
        }
        else
        {
            _AppendBoxMesh(_Mesh, 0.25 * kMachineDescriptionLengthToWorld, 0.25 * kMachineDescriptionLengthToWorld, 0.25 * kMachineDescriptionLengthToWorld);
        }
        return _Mesh;
    }

    iCAX::Render::SFloat3 _ToRenderFloat3(IN const iCAX::GeometryData::Point3& Point_) noexcept
    {
        return {
            static_cast<float>(Point_.X),
            static_cast<float>(Point_.Y),
            static_cast<float>(Point_.Z)
        };
    }

    iCAX::Render::SFloat2 _ToRenderFloat2(IN const iCAX::GeometryData::TextureCoordinate2& TextureCoordinate_) noexcept
    {
        return {
            static_cast<float>(TextureCoordinate_.U),
            static_cast<float>(TextureCoordinate_.V)
        };
    }

    iCAX::Render::SRenderMeshData _MakeRenderMeshFromTriangleMeshResource(
        IN const iCAX::GeometryData::CTriangleMeshResource& Resource_)
    {
        const auto& _Geometry = Resource_.Mesh;
        if (_Geometry.Vertices.empty() || _Geometry.Triangles.empty())
        {
            throw std::runtime_error("Machine triangle mesh resource does not contain triangles");
        }

        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;
        _Mesh.Positions.reserve(_Geometry.Vertices.size());
        for (const auto& _Point : _Geometry.Vertices)
        {
            _Mesh.Positions.push_back(_ToRenderFloat3(_Point));
        }

        if (_Geometry.Normals.size() == _Geometry.Vertices.size())
        {
            _Mesh.Normals.reserve(_Geometry.Normals.size());
            for (const auto& _Normal : _Geometry.Normals)
            {
                _Mesh.Normals.push_back({
                    static_cast<float>(_Normal.X),
                    static_cast<float>(_Normal.Y),
                    static_cast<float>(_Normal.Z)
                });
            }
            _Mesh.nFlags |= iCAX::Render::kRenderMeshFlagHasNormals;
        }

        if (_Geometry.TextureCoordinates.size() == _Geometry.Vertices.size())
        {
            _Mesh.TextureCoordinates.reserve(_Geometry.TextureCoordinates.size());
            for (const auto& _TextureCoordinate : _Geometry.TextureCoordinates)
            {
                _Mesh.TextureCoordinates.push_back(_ToRenderFloat2(_TextureCoordinate));
            }
            _Mesh.nFlags |= iCAX::Render::kRenderMeshFlagHasTextureCoordinates;
        }

        _Mesh.Indices.reserve(_Geometry.Triangles.size() * 3);
        for (const auto& _Triangle : _Geometry.Triangles)
        {
            for (const auto _Index : _Triangle)
            {
                if (_Index >= _Geometry.Vertices.size())
                {
                    throw std::runtime_error("Machine triangle mesh index is out of range");
                }
                _Mesh.Indices.push_back(_Index);
            }
        }
        return _Mesh;
    }

    std::array<double, 3> _ReadMachineGeometryScale(IN const ObjectMap& Source_)
    {
        const auto _Geometry = _TryGetObjectMapField(Source_, "geometry");
        if (!_Geometry || _GetObjectString(*_Geometry, "type") != "mesh")
        {
            return { 1.0, 1.0, 1.0 };
        }

        const auto _Scale = _ReadObjectDoubleArray(*_Geometry, "scale", 3, { 1.0, 1.0, 1.0 });
        return {
            _Scale[0] * kMachineDescriptionLengthToWorld,
            _Scale[1] * kMachineDescriptionLengthToWorld,
            _Scale[2] * kMachineDescriptionLengthToWorld
        };
    }

    iCAX::Data::Double4x4 _MakeAttachmentLocalMatrix(IN const ObjectMap& Source_)
    {
        const auto _Pose = _ReadPoseArray(Source_);
        const auto _Scale = _ReadMachineGeometryScale(Source_);
        return iCAX::Transform::MakeLocalMatrix(
            _Pose[0] * kMachineDescriptionLengthToWorld,
            _Pose[1] * kMachineDescriptionLengthToWorld,
            _Pose[2] * kMachineDescriptionLengthToWorld,
            _Pose[5],
            _Pose[4],
            _Pose[3],
            _Scale[0],
            _Scale[1],
            _Scale[2]);
    }

    iCAX::Render::SFloat3 _TransformPoint(
        IN const iCAX::Data::Double4x4& Matrix_,
        IN const iCAX::Render::SFloat3& Point_) noexcept
    {
        return {
            static_cast<float>(Matrix_(0, 0) * Point_.x + Matrix_(0, 1) * Point_.y + Matrix_(0, 2) * Point_.z + Matrix_(0, 3)),
            static_cast<float>(Matrix_(1, 0) * Point_.x + Matrix_(1, 1) * Point_.y + Matrix_(1, 2) * Point_.z + Matrix_(1, 3)),
            static_cast<float>(Matrix_(2, 0) * Point_.x + Matrix_(2, 1) * Point_.y + Matrix_(2, 2) * Point_.z + Matrix_(2, 3))
        };
    }

    void _AppendMeshIntoAggregate(
        IN OUT iCAX::Render::SRenderMeshData& Target_,
        IN const iCAX::Render::SRenderMeshData& Source_,
        IN const iCAX::Data::Double4x4& LocalMatrix_,
        IN uint32_t nVertexColorRGBA_)
    {
        if (Source_.Positions.empty())
        {
            return;
        }
        if (Target_.Positions.size() + Source_.Positions.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
        {
            throw std::runtime_error("Machine aggregate render mesh has too many vertices");
        }

        const auto _BaseVertex = static_cast<uint32_t>(Target_.Positions.size());
        Target_.Positions.reserve(Target_.Positions.size() + Source_.Positions.size());
        Target_.VertexColorsRGBA.reserve(Target_.VertexColorsRGBA.size() + Source_.Positions.size());
        for (const auto& _Position : Source_.Positions)
        {
            Target_.Positions.push_back(_TransformPoint(LocalMatrix_, _Position));
            Target_.VertexColorsRGBA.push_back(nVertexColorRGBA_);
        }

        if (!Source_.Indices.empty())
        {
            Target_.Indices.reserve(Target_.Indices.size() + Source_.Indices.size());
            for (const auto _Index : Source_.Indices)
            {
                if (_Index >= Source_.Positions.size())
                {
                    throw std::runtime_error("Machine aggregate render mesh source index is out of range");
                }
                Target_.Indices.push_back(_BaseVertex + _Index);
            }
        }
        else
        {
            Target_.Indices.reserve(Target_.Indices.size() + Source_.Positions.size());
            for (uint32_t _Index = 0; _Index < Source_.Positions.size(); ++_Index)
            {
                Target_.Indices.push_back(_BaseVertex + _Index);
            }
        }
        Target_.nFlags |= iCAX::Render::kRenderMeshFlagHasVertexColors;
    }

    iCAX::Render::SRenderMeshData _LoadMachineRenderMeshForAttachment(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strGeometryResourceID_)
    {
        if (auto _pRenderMesh = Scene_.Resources().Get<iCAX::Render::SRenderMeshData>(strGeometryResourceID_))
        {
            return *_pRenderMesh;
        }
        if (auto _pTriangleMesh = Scene_.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(strGeometryResourceID_))
        {
            return _MakeRenderMeshFromTriangleMeshResource(*_pTriangleMesh);
        }
        throw std::runtime_error("Machine attachment geometry resource is not renderable: " + strGeometryResourceID_);
    }

    uint32_t _MakeRGBA(
        IN double dRed_,
        IN double dGreen_,
        IN double dBlue_,
        IN double dAlpha_) noexcept
    {
        auto _ToByte = [](double dValue_) -> uint32_t {
            return static_cast<uint32_t>(std::clamp(dValue_, 0.0, 1.0) * 255.0 + 0.5);
        };
        return (_ToByte(dRed_) << 24) | (_ToByte(dGreen_) << 16) | (_ToByte(dBlue_) << 8) | _ToByte(dAlpha_);
    }

    uint32_t _ReadVisualMaterialColor(
        IN const ObjectMap& Visual_,
        IN const std::string& strFieldName_,
        IN const std::vector<double>& Default_) noexcept
    {
        try
        {
            if (const auto _Material = _TryGetObjectMapField(Visual_, "material"))
            {
                const auto _Color = _ReadObjectDoubleArray(*_Material, strFieldName_, 4, Default_);
                const auto _Transparency = _GetObjectDoubleOrDefault(Visual_, "transparency", 0.0);
                return _MakeRGBA(_Color[0], _Color[1], _Color[2], _Color[3] * (1.0 - std::clamp(_Transparency, 0.0, 1.0)));
            }
        }
        catch (const std::exception&)
        {
        }
        return _MakeRGBA(Default_[0], Default_[1], Default_[2], Default_[3]);
    }

    uint32_t _ReadVisualColor(IN const ObjectMap& Visual_) noexcept
    {
        return _ReadVisualMaterialColor(Visual_, "diffuse", { 0.65, 0.70, 0.72, 1.0 });
    }

    uint32_t _ReadVisualAmbientColor(IN const ObjectMap& Visual_) noexcept
    {
        return _ReadVisualMaterialColor(Visual_, "ambient", { 0.65, 0.70, 0.72, 1.0 });
    }

    uint32_t _ReadVisualSpecularColor(IN const ObjectMap& Visual_) noexcept
    {
        return _ReadVisualMaterialColor(Visual_, "specular", { 0.0, 0.0, 0.0, 1.0 });
    }

    uint32_t _ReadVisualEmissiveColor(IN const ObjectMap& Visual_) noexcept
    {
        return _ReadVisualMaterialColor(Visual_, "emissive", { 0.0, 0.0, 0.0, 1.0 });
    }

    bool _ReadVisualLighting(IN const ObjectMap& Visual_) noexcept
    {
        try
        {
            if (const auto _Material = _TryGetObjectMapField(Visual_, "material"))
            {
                const auto _Iter = _Material->find("lighting");
                if (_Iter != _Material->end() && _Iter->second.Is<bool>())
                {
                    return _Iter->second.To<bool>();
                }
            }
        }
        catch (const std::exception&)
        {
        }
        return true;
    }

    std::string _ReadVisualBaseColorTextureURI(IN const ObjectMap& Visual_) noexcept
    {
        try
        {
            if (const auto _Material = _TryGetObjectMapField(Visual_, "material"))
            {
                return _GetObjectString(*_Material, "baseColorTextureUri");
            }
        }
        catch (const std::exception&)
        {
        }
        return {};
    }

    uint64_t _NextMachineRenderResourceVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    iCAX::Render::SRenderMaterialData _MakeMachineMaterialData(
        IN const ObjectMap& Source_,
        IN const std::string& strBaseColorTextureResourceID_) noexcept
    {
        iCAX::Render::SRenderMaterialData _Material;
        _Material.nDataVersion = _NextMachineRenderResourceVersion();
        _Material.nColorRGBA = _ReadVisualColor(Source_);
        _Material.nAmbientRGBA = _ReadVisualAmbientColor(Source_);
        _Material.nSpecularRGBA = _ReadVisualSpecularColor(Source_);
        _Material.nEmissiveRGBA = _ReadVisualEmissiveColor(Source_);
        _Material.nLineWidth = 1.0f;
        _Material.nMaterialFlags = _ReadVisualLighting(Source_) ? 0u : iCAX::Render::kRenderStyleFlagLightingDisabled;
        if (!strBaseColorTextureResourceID_.empty())
        {
            _Material.strBaseColorTextureResourceID = strBaseColorTextureResourceID_;
            _Material.nMaterialFlags |= iCAX::Render::kRenderStyleFlagHasBaseColorTexture;
        }
        return _Material;
    }

    bool _GetObjectBoolOrDefault(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN bool bDefault_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return bDefault_;
        }
        if (_Iter->second.Is<bool>())
        {
            return _Iter->second.To<bool>();
        }
        return bDefault_;
    }

    std::string _MakeMachineGeometryResourceID(
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strAttachmentKind_,
        IN size_t nIndex_)
    {
        return "machine/" + iCAX::Data::to_string(MachineID_) + "/" + strAttachmentKind_ + "/" + std::to_string(nIndex_) + "#render.mesh";
    }

    std::string _NormalizeFileUri(IN std::string Uri_)
    {
        constexpr const char* _FilePrefix = "file://";
        if (Uri_.rfind(_FilePrefix, 0) != 0)
        {
            return Uri_;
        }

        Uri_.erase(0, std::char_traits<char>::length(_FilePrefix));
        if (Uri_.size() >= 3 && Uri_[0] == '/' && std::isalpha(static_cast<unsigned char>(Uri_[1])) && Uri_[2] == ':')
        {
            Uri_.erase(0, 1);
        }
        std::replace(Uri_.begin(), Uri_.end(), '/', '\\');
        return Uri_;
    }

    std::string _StripUriScheme(IN const std::string& Uri_, IN const std::string& Scheme_)
    {
        return Uri_.rfind(Scheme_, 0) == 0 ? Uri_.substr(Scheme_.size()) : Uri_;
    }

    void _AppendPathCandidate(
        IN OUT std::vector<std::filesystem::path>& Candidates_,
        IN const std::filesystem::path& Path_)
    {
        if (!Path_.empty())
        {
            Candidates_.push_back(Path_.lexically_normal());
        }
    }

    std::string _ResolveMachineResourceUri(
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& Uri_,
        IN const std::string& strResourceKind_)
    {
        if (Uri_.empty())
        {
            throw std::invalid_argument("Machine " + strResourceKind_ + " requires uri");
        }

        const std::filesystem::path _BaseDirectory(Description_.SourceDirectory.empty()
            ? std::filesystem::path(Description_.SourcePath).parent_path()
            : std::filesystem::path(Description_.SourceDirectory));
        const auto _FileUri = _NormalizeFileUri(Uri_);
        std::vector<std::filesystem::path> _Candidates;

        if (_FileUri.rfind("model://", 0) == 0 || _FileUri.rfind("package://", 0) == 0)
        {
            const auto _WithoutScheme = _StripUriScheme(_StripUriScheme(_FileUri, "model://"), "package://");
            const std::filesystem::path _Relative(_WithoutScheme);
            _AppendPathCandidate(_Candidates, _BaseDirectory / _Relative);
            _AppendPathCandidate(_Candidates, _BaseDirectory.parent_path() / _Relative);

            auto _Iter = _Relative.begin();
            if (_Iter != _Relative.end())
            {
                const auto _First = (*_Iter).string();
                ++_Iter;
                std::filesystem::path _Tail;
                for (; _Iter != _Relative.end(); ++_Iter)
                {
                    _Tail /= *_Iter;
                }
                if (!_Tail.empty() && (_First == Description_.ModelName || !_First.empty()))
                {
                    _AppendPathCandidate(_Candidates, _BaseDirectory / _Tail);
                }
            }
        }
        else
        {
            const std::filesystem::path _Path(_FileUri);
            if (_Path.is_absolute())
            {
                _AppendPathCandidate(_Candidates, _Path);
            }
            else
            {
                _AppendPathCandidate(_Candidates, _BaseDirectory / _Path);
                _AppendPathCandidate(_Candidates, _BaseDirectory.parent_path() / _Path);
            }
        }

        for (const auto& _Candidate : _Candidates)
        {
            if (std::filesystem::exists(_Candidate))
            {
                return _Candidate.string();
            }
        }

        std::ostringstream _Stream;
        _Stream << "Machine " << strResourceKind_ << " uri cannot be resolved: " << Uri_;
        for (const auto& _Candidate : _Candidates)
        {
            _Stream << " | tried=" << _Candidate.string();
        }
        throw std::runtime_error(_Stream.str());
    }

    std::string _ResolveMachineMeshUri(
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& Uri_)
    {
        return _ResolveMachineResourceUri(Description_, Uri_, "mesh");
    }

    std::string _ResolveMachineTextureUri(
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& Uri_)
    {
        return _ResolveMachineResourceUri(Description_, Uri_, "texture");
    }

    std::string _FindMachineImportedResourceID(
        IN const iCAX::Resource::CResourceImportResult& Result_,
        IN const std::string& Role_)
    {
        const auto _Iter = std::find_if(Result_.Items.begin(), Result_.Items.end(), [&Role_](IN const iCAX::Resource::CResourceImportItem& Item_) {
            return Item_.Role == Role_;
        });
        return _Iter == Result_.Items.end() ? std::string() : _Iter->ResourceID;
    }

    std::string _GetMachineMeshFormatID(IN const std::string& SourcePath_)
    {
        auto _Extension = std::filesystem::path(SourcePath_).extension().string();
        std::transform(_Extension.begin(), _Extension.end(), _Extension.begin(), [](IN unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });

        if (_Extension == ".stl")
        {
            return "geometry.stl";
        }
        if (_Extension == ".dae")
        {
            return "geometry.dae";
        }
        return {};
    }

    std::string _ImportMachineExternalMeshResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const ObjectMap& Source_)
    {
        const auto _Geometry = _TryGetObjectMapField(Source_, "geometry");
        if (!_Geometry)
        {
            throw std::invalid_argument("Machine mesh source has no geometry object");
        }

        const auto _Uri = _GetObjectString(*_Geometry, "uri");
        const auto _ResolvedPath = _ResolveMachineMeshUri(Description_, _Uri);
        const auto _FormatID = _GetMachineMeshFormatID(_ResolvedPath);
        if (_FormatID.empty())
        {
            throw std::runtime_error("Machine mesh geometry format is not supported: " + _ResolvedPath);
        }

        iCAX::Resource::CResourceImportRequest _Request;
        _Request.SourcePath = _ResolvedPath;
        _Request.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        _Request.FormatID = _FormatID;
        _Request.Options["sourceUri"] = _Uri;
        _Request.Options["machineDescriptionSource"] = Description_.SourcePath;

        iCAX::Resource::CResourceImportResult _Result;
        auto _pMesh = Scene_.Resources().Import<iCAX::GeometryData::CTriangleMeshResource>(_Request, &_Result);
        if (!_pMesh)
        {
            throw std::runtime_error("Machine mesh import failed: " + (_Result.Error.empty() ? _ResolvedPath : _Result.Error));
        }

        auto _ResourceID = _FindMachineImportedResourceID(_Result, "geometry.triangle_mesh");
        if (_ResourceID.empty())
        {
            _ResourceID = _Result.PrimaryResourceID;
        }
        if (_ResourceID.empty() || !Scene_.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(_ResourceID))
        {
            throw std::runtime_error("Machine mesh import did not return triangle mesh resource: " + _ResolvedPath);
        }
        return _ResourceID;
    }

    std::string _CreateMachinePrimitiveRenderMeshResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strMachineName_,
        IN const std::string& strLinkName_,
        IN const std::string& strNodeName_,
        IN const ObjectMap& Source_,
        IN const std::string& strAttachmentKind_,
        IN size_t nIndex_)
    {
        const auto _ResourceID = _MakeMachineGeometryResourceID(MachineID_, strAttachmentKind_, nIndex_);
        auto _Mesh = std::make_shared<iCAX::Render::SRenderMeshData>(_MakeMachinePrimitiveRenderMesh(Source_));
        _Mesh->nDataVersion = _NextMachineRenderResourceVersion();

        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = _ResourceID;
        _Info.Name = (strMachineName_.empty() ? std::string("Machine") : strMachineName_) + "/" + strLinkName_ + "/" + strNodeName_;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        _Info.nVersion = _Mesh->nDataVersion;
        _Info.Metadata["kind"] = "render.mesh";
        _Info.Metadata["attachmentKind"] = strAttachmentKind_;
        Scene_.Resources().Set<iCAX::Render::SRenderMeshData>(_ResourceID, _Mesh, _Info);
        return _ResourceID;
    }

    std::string _CreateMachineTextureResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& strMachineResourceID_,
        IN const std::string& strMachineName_,
        IN const std::string& strLinkName_,
        IN const std::string& strNodeName_,
        IN const ObjectMap& Source_,
        IN const std::string& strAttachmentKind_,
        IN size_t nIndex_)
    {
        const auto _TextureURI = _ReadVisualBaseColorTextureURI(Source_);
        if (_TextureURI.empty())
        {
            return {};
        }

        const auto _ResolvedPath = _ResolveMachineTextureUri(Description_, _TextureURI);
        const auto _ResourceID = iCAX::CAM::MakeMachineTextureResourceID(strMachineResourceID_, strAttachmentKind_, nIndex_);
        if (_ResourceID.empty())
        {
            throw std::invalid_argument("Machine texture resource id cannot be empty");
        }

        auto _Texture = std::make_shared<iCAX::Render::SRenderTextureData>();
        _Texture->nDataVersion = _NextMachineRenderResourceVersion();
        _Texture->strSourceURI = _ResolvedPath;

        auto _Info = _MakeResourceInfo(
            _ResourceID,
            (strMachineName_.empty() ? std::string("Machine") : strMachineName_) + "/" + strLinkName_ + "/" + strNodeName_ + " Texture",
            iCAX::Render::SRenderTextureData::kResourceTypeName,
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Texture->nDataVersion);
        _Info.Source = _ResolvedPath;
        _Info.Metadata["attachmentKind"] = strAttachmentKind_;
        _Info.Metadata["linkName"] = strLinkName_;
        _Info.Metadata["nodeName"] = strNodeName_;
        _Info.Metadata["originalUri"] = _TextureURI;
        _Info.Metadata["resolvedPath"] = _ResolvedPath;
        Scene_.Resources().Set<iCAX::Render::SRenderTextureData>(_ResourceID, _Texture, _Info);
        return _ResourceID;
    }

    std::string _CreateMachineMaterialResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& strMachineResourceID_,
        IN const std::string& strMachineName_,
        IN const std::string& strLinkName_,
        IN const std::string& strNodeName_,
        IN const ObjectMap& Source_,
        IN const std::string& strAttachmentKind_,
        IN size_t nIndex_)
    {
        const auto _ResourceID = iCAX::CAM::MakeMachineMaterialResourceID(strMachineResourceID_, strAttachmentKind_, nIndex_);
        if (_ResourceID.empty())
        {
            throw std::invalid_argument("Machine material resource id cannot be empty");
        }

        const auto _TextureResourceID = _CreateMachineTextureResource(
            Scene_,
            Description_,
            strMachineResourceID_,
            strMachineName_,
            strLinkName_,
            strNodeName_,
            Source_,
            strAttachmentKind_,
            nIndex_);
        auto _Material = std::make_shared<iCAX::Render::SRenderMaterialData>(_MakeMachineMaterialData(Source_, _TextureResourceID));
        auto _Info = _MakeResourceInfo(
            _ResourceID,
            (strMachineName_.empty() ? std::string("Machine") : strMachineName_) + "/" + strLinkName_ + "/" + strNodeName_ + " Material",
            iCAX::Render::SRenderMaterialData::kResourceTypeName,
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _Material->nDataVersion);
        _Info.Metadata["attachmentKind"] = strAttachmentKind_;
        _Info.Metadata["linkName"] = strLinkName_;
        _Info.Metadata["nodeName"] = strNodeName_;
        if (!_TextureResourceID.empty())
        {
            _Info.Metadata["baseColorTextureResourceID"] = _TextureResourceID;
        }
        Scene_.Resources().Set<iCAX::Render::SRenderMaterialData>(_ResourceID, _Material, _Info);
        return _ResourceID;
    }

    void _SetTransformFromPose(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_,
        IN const std::array<double, 6>& Pose_)
    {
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_PositionX, Pose_[0] * kMachineDescriptionLengthToWorld);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_PositionY, Pose_[1] * kMachineDescriptionLengthToWorld);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_PositionZ, Pose_[2] * kMachineDescriptionLengthToWorld);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_RollRadians, Pose_[3]);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_PitchRadians, Pose_[4]);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_YawRadians, Pose_[5]);
    }

    void _ApplyMachineMeshScale(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_,
        IN const ObjectMap& Source_)
    {
        const auto _Geometry = _TryGetObjectMapField(Source_, "geometry");
        if (!_Geometry || _GetObjectString(*_Geometry, "type") != "mesh")
        {
            return;
        }

        const auto _Scale = _ReadObjectDoubleArray(*_Geometry, "scale", 3, { 1.0, 1.0, 1.0 });
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_ScaleX, _Scale[0] * kMachineDescriptionLengthToWorld);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_ScaleY, _Scale[1] * kMachineDescriptionLengthToWorld);
        _SetDoubleProperty(pTransform_, iCAX::Transform::CTransformComponent::PropertyName_ScaleZ, _Scale[2] * kMachineDescriptionLengthToWorld);
    }

    void _SetTransformParent(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& ChildEntityID_,
        IN const iCAX::Data::uuid& ParentEntityID_)
    {
        std::string _strError;
        if (!iCAX::Transform::SetTransformParent(Repository_, ChildEntityID_, ParentEntityID_, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Set CAM machine transform parent failed" : _strError);
        }
    }

    void _ClearTransformChildren(
        IN const std::shared_ptr<iCAX::Transform::CTransformComponent>& pTransform_)
    {
        _SetVariantArrayProperty(
            pTransform_,
            iCAX::Transform::CTransformComponent::PropertyName_ChildEntityIDs,
            VariantArray{});
    }

    std::shared_ptr<iCAX::Database::IEntity> _CreateMachinePartTransformEntity(
        IN iCAX::Database::IRepository& Repository_)
    {
        std::shared_ptr<iCAX::Database::IEntity> _pEntity;
        std::string _strError;
        const auto _EntityID = iCAX::Data::GenerateNewUUID();
        if (!Repository_.CreateEntity(_EntityID, _pEntity, _strError) || !_pEntity)
        {
            throw std::runtime_error(_strError.empty() ? "Create CAM machine part entity failed" : _strError);
        }
        (void)_AddComponent<iCAX::Transform::CTransformComponent>(_pEntity);
        return _pEntity;
    }

    void _AttachMachinePartIdentity(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const iCAX::CAM::SMachineElementData& Element_,
        IN const std::string& strPartName_,
        IN size_t nSourceIndex_)
    {
        auto _pElement = _GetOrAddEntityComponent<iCAX::CAM::CMachineElementComponent>(pEntity_);
        auto _pLink = _GetOrAddEntityComponent<iCAX::CAM::CMachineLinkComponent>(pEntity_);
        _SetUuidProperty(_pElement, iCAX::CAM::CMachineElementComponent::PropertyName_MachineID, MachineID_);
        _SetStringProperty(_pElement, iCAX::CAM::CMachineElementComponent::PropertyName_ElementKind, "part");
        _SetStringProperty(_pElement, iCAX::CAM::CMachineElementComponent::PropertyName_Name, strPartName_);
        _SetUInt64Property(_pElement, iCAX::CAM::CMachineElementComponent::PropertyName_SourceIndex, static_cast<unsigned long long>(nSourceIndex_));
        _SetStringProperty(_pLink, iCAX::CAM::CMachineLinkComponent::PropertyName_LinkName, Element_.OriginalName.empty() ? strPartName_ : Element_.OriginalName);
        _SetBoolProperty(_pLink, iCAX::CAM::CMachineLinkComponent::PropertyName_SelfCollide, Element_.bSelfCollide);
        _SetBoolProperty(_pLink, iCAX::CAM::CMachineLinkComponent::PropertyName_Gravity, Element_.bGravity);
        _SetBoolProperty(_pLink, iCAX::CAM::CMachineLinkComponent::PropertyName_Kinematic, Element_.bKinematic);

        if (Element_.Tool.bValid)
        {
            auto _pTool = _GetOrAddEntityComponent<iCAX::CAM::CMachineToolComponent>(pEntity_);
            _SetStringProperty(_pTool, iCAX::CAM::CMachineToolComponent::PropertyName_ToolName, Element_.Tool.Name.empty() ? strPartName_ : Element_.Tool.Name);
            _SetStringProperty(_pTool, iCAX::CAM::CMachineToolComponent::PropertyName_ToolType, Element_.Tool.ToolType.empty() ? std::string("tool") : Element_.Tool.ToolType);
            _SetVariantArrayProperty(_pTool, iCAX::CAM::CMachineToolComponent::PropertyName_TcpLocalPosition, Element_.Tool.TcpLocalPosition);
            _SetVariantArrayProperty(_pTool, iCAX::CAM::CMachineToolComponent::PropertyName_BeamLocalDirection, Element_.Tool.BeamLocalDirection);
        }
    }

    void _DeleteMachineElementEntities(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        auto _Entities = Repository_.FilterEntities([&MachineID_](std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
            auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(pEntity_);
            return _pElement && _pElement->GetMachineID() == MachineID_;
        });

        for (const auto& _pEntity : _Entities)
        {
            std::string _strError;
            if (!_pEntity || !Repository_.DeleteEntity(_pEntity->GetID(), _strError))
            {
                throw std::runtime_error(_strError.empty() ? "Delete CAM machine element entity failed" : _strError);
            }
        }
    }

    bool _IsMachinePartElement(IN const iCAX::CAM::SMachineElementData& Element_)
    {
        return Element_.ElementKind == "part";
    }

    std::unordered_map<std::string, iCAX::Data::uuid> _CreateMachinePartEntities(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::vector<iCAX::CAM::SMachineElementData>& Elements_)
    {
        std::unordered_map<std::string, iCAX::Data::uuid> _ElementEntities;

        // First pass: create the Transform tree nodes before attaching machine identity components.
        // MachineElement constraints lock part Transform editing after construction, so initial pose and parent
        // relationship must be established while the entity is still a neutral Transform node.
        for (size_t _Index = 0; _Index < Elements_.size(); ++_Index)
        {
            const auto& _Element = Elements_[_Index];
            if (!_IsMachinePartElement(_Element))
            {
                continue;
            }

            auto _PartName = _Element.Name.empty() ? std::string("Part") + std::to_string(_Index + 1) : _Element.Name;
            auto _pLinkEntity = _CreateMachinePartTransformEntity(Repository_);
            auto _pTransform = _GetOrAddEntityComponent<iCAX::Transform::CTransformComponent>(_pLinkEntity);
            _SetTransformParent(Repository_, _pLinkEntity->GetID(), MachineID_);
            // Transform 表达运行时父子边的局部矩阵。有关节约束的元素使用 JointToParent.Pose；
            // 没有关节的根部件才使用元素自身 pose。
            _SetTransformFromPose(_pTransform, _Element.JointToParent.bValid ? _Element.JointToParent.Pose : _Element.Pose);
            _ElementEntities[_Element.ElementID] = _pLinkEntity->GetID();
        }

        // Second pass: apply the only machine hierarchy from the neutral resource.
        // Joint data is attached separately to the child element; the parent-child relation itself is Transform.
        for (const auto& _Element : Elements_)
        {
            if (!_IsMachinePartElement(_Element))
            {
                continue;
            }

            auto _EntityIter = _ElementEntities.find(_Element.ElementID);
            if (_EntityIter == _ElementEntities.end())
            {
                continue;
            }

            auto _ParentID = MachineID_;
            auto _ParentIter = _ElementEntities.find(_Element.ParentElementID);
            if (_ParentIter != _ElementEntities.end())
            {
                _ParentID = _ParentIter->second;
            }
            _SetTransformParent(Repository_, _EntityIter->second, _ParentID);
        }

        // Third pass: attach business components after the Transform tree is complete.
        for (size_t _Index = 0; _Index < Elements_.size(); ++_Index)
        {
            const auto& _Element = Elements_[_Index];
            if (!_IsMachinePartElement(_Element))
            {
                continue;
            }

            auto _EntityIter = _ElementEntities.find(_Element.ElementID);
            if (_EntityIter == _ElementEntities.end())
            {
                continue;
            }

            auto _pLinkEntity = Repository_.GetEntity(_EntityIter->second);
            auto _PartName = _Element.Name.empty() ? std::string("Part") + std::to_string(_Index + 1) : _Element.Name;
            _AttachMachinePartIdentity(_pLinkEntity, MachineID_, _Element, _PartName, _Index);
        }

        return _ElementEntities;
    }

    void _CreateMachineJointComponents(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::unordered_map<std::string, iCAX::Data::uuid>& ElementEntities_,
        IN const std::vector<iCAX::CAM::SMachineElementData>& Elements_)
    {
        (void)MachineID_;
        for (size_t _Index = 0; _Index < Elements_.size(); ++_Index)
        {
            const auto& _Element = Elements_[_Index];
            const auto& _Joint = _Element.JointToParent;
            if (!_Joint.bValid)
            {
                continue;
            }

            auto _JointName = _Joint.Name;
            if (_JointName.empty())
            {
                _JointName = "Joint" + std::to_string(_Index + 1);
            }
            const auto _JointType = _Joint.JointType;
            const auto _ChildIter = ElementEntities_.find(_Element.ElementID);
            if (_ChildIter == ElementEntities_.end())
            {
                throw std::runtime_error("CAM machine joint child element does not exist: " + _Element.ElementID);
            }

            auto _pChildLinkEntity = Repository_.GetEntity(_ChildIter->second);
            if (!_pChildLinkEntity)
            {
                throw std::runtime_error("CAM machine joint child entity does not exist: " + _Element.ElementID);
            }

            auto _pJoint = _GetOrAddEntityComponent<iCAX::CAM::CMachineJointComponent>(_pChildLinkEntity);

            auto [_Lower, _Upper] = _IsMovableMachineJoint(_JointType)
                ? _DefaultMachineJointLimits(_JointType)
                : std::pair<double, double>{ 0.0, 0.0 };
            VariantArray _Axis = _Joint.Axis.bValid ? _Joint.Axis.XYZ : _MakeDoubleArray({ 0.0, 0.0, 1.0 });
            VariantArray _Axis2 = _Joint.Axis2.bValid ? _Joint.Axis2.XYZ : VariantArray{};
            if (_Joint.Axis.bValid)
            {
                _Lower = _Joint.dLowerLimit;
                _Upper = _Joint.dUpperLimit;
                if (_JointType == "prismatic")
                {
                    _Lower *= kMachineDescriptionLengthToWorld;
                    _Upper *= kMachineDescriptionLengthToWorld;
                }
            }
            if (_Upper < _Lower)
            {
                std::swap(_Lower, _Upper);
            }

            _SetStringProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_JointName, _JointName);
            _SetStringProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_JointType, _JointType);
            _SetStringProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_ParentLinkName, _Joint.OriginalParentName);
            _SetStringProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_ChildLinkName, _Joint.OriginalChildName);
            _SetVariantArrayProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_Axis, _Axis);
            _SetVariantArrayProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_Axis2, _Axis2);
            _SetDoubleProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_LowerLimit, _Lower);
            _SetDoubleProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_UpperLimit, _Upper);
            _SetDoubleProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_Position, 0.0);
        }
    }

    ObjectMap _MakeMachineVisualSource(
        IN const iCAX::CAM::SMachineElementData& Owner_,
        IN const iCAX::CAM::SMachineVisualData& Visual_);

    ObjectMap _MakeMachineCollisionSource(
        IN const iCAX::CAM::SMachineElementData& Owner_,
        IN const iCAX::CAM::SMachineCollisionData& Collision_);

    std::string _MakeMachineElementRenderMeshResourceID(
        IN const iCAX::Data::uuid& MachineID_,
        IN const iCAX::Data::uuid& ElementEntityID_)
    {
        return "machine/" + iCAX::Data::to_string(MachineID_) + "/element/" + iCAX::Data::to_string(ElementEntityID_) + "#render.mesh";
    }

    ObjectMap _MakeVisualAttachmentPayload(
        IN const ObjectMap& Source_,
        IN const std::string& strOwnerName_,
        IN const std::string& strNodeName_,
        IN const std::string& strGeometryResourceID_,
        IN const std::string& strMaterialResourceID_)
    {
        ObjectMap _Visual;
        _Visual["name"] = strNodeName_;
        _Visual["visualName"] = strNodeName_;
        _Visual["linkName"] = strOwnerName_;
        _Visual["pose"] = Source_.at("pose");
        _Visual["geometry"] = Source_.at("geometry");
        const auto _MaterialIter = Source_.find("material");
        _Visual["material"] = _MaterialIter == Source_.end() ? ObjectMap{} : _MaterialIter->second;
        _Visual["geometryResourceId"] = strGeometryResourceID_;
        _Visual["materialResourceId"] = strMaterialResourceID_;
        _Visual["castShadows"] = _GetObjectBoolOrDefault(Source_, "castShadows", true);
        _Visual["transparency"] = _GetObjectDoubleOrDefault(Source_, "transparency", 0.0);
        return _Visual;
    }

    ObjectMap _MakeCollisionAttachmentPayload(
        IN const ObjectMap& Source_,
        IN const std::string& strOwnerName_,
        IN const std::string& strNodeName_,
        IN const std::string& strGeometryResourceID_)
    {
        ObjectMap _Collision;
        _Collision["name"] = strNodeName_;
        _Collision["collisionName"] = strNodeName_;
        _Collision["linkName"] = strOwnerName_;
        _Collision["pose"] = Source_.at("pose");
        _Collision["geometry"] = Source_.at("geometry");
        _Collision["geometryResourceId"] = strGeometryResourceID_;
        return _Collision;
    }

    std::string _CreateMachineAttachmentGeometryResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strMachineName_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const ObjectMap& Source_,
        IN const std::string& strAttachmentKind_,
        IN size_t nIndex_)
    {
        const auto _OwnerName = _GetObjectString(Source_, "ownerName");
        auto _NodeName = _GetObjectString(Source_, "name");
        if (_NodeName.empty())
        {
            _NodeName = strAttachmentKind_ + std::to_string(nIndex_ + 1);
        }

        return _IsMachineExternalMeshGeometry(Source_)
            ? _ImportMachineExternalMeshResource(Scene_, Description_, Source_)
            : _CreateMachinePrimitiveRenderMeshResource(Scene_, MachineID_, strMachineName_, _OwnerName, _NodeName, Source_, strAttachmentKind_, nIndex_);
    }

    void _RebuildMachineElementRenderMesh(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pElementEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineElementComponent>& pElement_)
    {
        if (!pElementEntity_ || !pElement_)
        {
            return;
        }

        auto _pAppearance = _GetComponent<iCAX::CAM::CMachineAppearanceComponent>(pElementEntity_);
        const auto _bUseColorOverride = _pAppearance && _pAppearance->GetUseColorOverride();
        const auto _OverrideColor = _pAppearance ? static_cast<uint32_t>(_pAppearance->GetColorRGBA() & 0xFFFFFFFFull) : 0x8FB8C9FFu;

        iCAX::Render::SRenderMeshData _AggregateMesh;
        _AggregateMesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;

        auto _AppendAttachment = [&Scene_, &_AggregateMesh](
            IN const ObjectMap& Attachment_,
            IN uint32_t nColorRGBA_)
        {
            const auto _GeometryResourceID = _GetObjectString(Attachment_, "geometryResourceId");
            if (_GeometryResourceID.empty())
            {
                return;
            }
            _AppendMeshIntoAggregate(
                _AggregateMesh,
                _LoadMachineRenderMeshForAttachment(Scene_, _GeometryResourceID),
                _MakeAttachmentLocalMatrix(Attachment_),
                nColorRGBA_);
        };

        if (auto _pVisuals = _GetComponent<iCAX::CAM::CMachineVisualComponent>(pElementEntity_))
        {
            for (const auto& _VisualVariant : _pVisuals->GetVisuals())
            {
                if (!_VisualVariant.Is<ObjectMap>())
                {
                    continue;
                }
                const auto _Visual = _VisualVariant.To<ObjectMap>();
                _AppendAttachment(_Visual, _bUseColorOverride ? _OverrideColor : _ReadVisualColor(_Visual));
            }
        }

        auto _pRender = _GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(pElementEntity_);
        if (_AggregateMesh.Positions.empty())
        {
            if (_pRender)
            {
                _SetBoolProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_Visible, false);
            }
            return;
        }

        _AggregateMesh.nDataVersion = _NextMachineRenderResourceVersion();
        const auto _MachineID = pElement_->GetMachineID();
        const auto _RenderMeshResourceID = _MakeMachineElementRenderMeshResourceID(_MachineID, pElementEntity_->GetID());
        auto _pAggregateMesh = std::make_shared<iCAX::Render::SRenderMeshData>(_AggregateMesh);

        auto _pMachineEntity = Scene_.Database().GetEntity(_MachineID);
        auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
        const auto _MachineName = _pMachine ? _pMachine->GetName() : std::string("Machine");
        auto _Info = _MakeResourceInfo(
            _RenderMeshResourceID,
            _MachineName + "/" + (pElement_->GetName().empty() ? std::string("Element") : pElement_->GetName()) + " Render Mesh",
            "render.mesh",
            iCAX::Resource::EResourcePersistenceMode::RuntimeOnly,
            _AggregateMesh.nDataVersion);
        _Info.Metadata["kind"] = "render.mesh";
        _Info.Metadata["machineEntityID"] = iCAX::Data::to_string(_MachineID);
        _Info.Metadata["elementEntityID"] = iCAX::Data::to_string(pElementEntity_->GetID());
        _Info.Metadata["elementName"] = pElement_->GetName();
        Scene_.Resources().Set<iCAX::Render::SRenderMeshData>(_RenderMeshResourceID, _pAggregateMesh, _Info);

        _pRender = _GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(pElementEntity_);
        _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, _RenderMeshResourceID);
        _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_MaterialResourceID, std::string());
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryKind, 1ull);
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_RenderClass, 4ull);
        _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_StyleID, 1000ull + pElement_->GetSourceIndex());
        _SetBoolProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_Visible, true);
        _SetBoolProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_Selectable, true);
    }

    void _AttachMachineElementGeometry(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strMachineResourceID_,
        IN const std::string& strMachineName_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::unordered_map<std::string, iCAX::Data::uuid>& ElementEntities_,
        IN const iCAX::CAM::SMachineElementData& Element_,
        IN OUT size_t& nVisualIndex_,
        IN OUT size_t& nCollisionIndex_,
        IN bool bRenderCollisions_)
    {
        auto _ElementIter = ElementEntities_.find(Element_.ElementID);
        if (_ElementIter == ElementEntities_.end())
        {
            return;
        }

        auto& _Repository = Scene_.Database();
        auto _pElementEntity = _Repository.GetEntity(_ElementIter->second);
        if (!_pElementEntity)
        {
            throw std::runtime_error("CAM machine element entity does not exist while attaching geometry: " + Element_.ElementID);
        }

        VariantArray _VisualAttachments;
        VariantArray _CollisionAttachments;
        (void)bRenderCollisions_;

        for (const auto& _Visual : Element_.Visuals)
        {
            auto _Source = _MakeMachineVisualSource(Element_, _Visual);
            auto _NodeName = _GetObjectString(_Source, "name");
            if (_NodeName.empty())
            {
                _NodeName = "visual" + std::to_string(nVisualIndex_ + 1);
            }
            const auto _OwnerName = _GetObjectString(_Source, "ownerName");
            const auto _GeometryResourceID = _CreateMachineAttachmentGeometryResource(
                Scene_,
                MachineID_,
                strMachineName_,
                Description_,
                _Source,
                "visual",
                nVisualIndex_);
            const auto _MaterialResourceID = _CreateMachineMaterialResource(
                Scene_,
                Description_,
                strMachineResourceID_,
                strMachineName_,
                _OwnerName,
                _NodeName,
                _Source,
                "visual",
                nVisualIndex_);

            _VisualAttachments.emplace_back(_MakeVisualAttachmentPayload(
                _Source,
                _OwnerName,
                _NodeName,
                _GeometryResourceID,
                _MaterialResourceID));
            ++nVisualIndex_;
        }

        for (const auto& _Collision : Element_.Collisions)
        {
            auto _Source = _MakeMachineCollisionSource(Element_, _Collision);
            auto _NodeName = _GetObjectString(_Source, "name");
            if (_NodeName.empty())
            {
                _NodeName = "collision" + std::to_string(nCollisionIndex_ + 1);
            }
            const auto _OwnerName = _GetObjectString(_Source, "ownerName");
            const auto _GeometryResourceID = _CreateMachineAttachmentGeometryResource(
                Scene_,
                MachineID_,
                strMachineName_,
                Description_,
                _Source,
                "collision",
                nCollisionIndex_);

            _CollisionAttachments.emplace_back(_MakeCollisionAttachmentPayload(
                _Source,
                _OwnerName,
                _NodeName,
                _GeometryResourceID));
            ++nCollisionIndex_;
        }

        if (!_VisualAttachments.empty())
        {
            auto _pVisualComponent = _GetOrAddEntityComponent<iCAX::CAM::CMachineVisualComponent>(_pElementEntity);
            _SetVariantArrayProperty(_pVisualComponent, iCAX::CAM::CMachineVisualComponent::PropertyName_Visuals, _VisualAttachments);
        }
        if (!_CollisionAttachments.empty())
        {
            auto _pCollisionComponent = _GetOrAddEntityComponent<iCAX::CAM::CMachineCollisionComponent>(_pElementEntity);
            _SetVariantArrayProperty(_pCollisionComponent, iCAX::CAM::CMachineCollisionComponent::PropertyName_Collisions, _CollisionAttachments);
        }
        _GetOrAddEntityComponent<iCAX::CAM::CMachineAppearanceComponent>(_pElementEntity);
        _RebuildMachineElementRenderMesh(
            Scene_,
            _pElementEntity,
            _GetComponent<iCAX::CAM::CMachineElementComponent>(_pElementEntity));
    }

    VariantArray _PoseToVariantArray(IN const std::array<double, 6>& Pose_)
    {
        VariantArray _Result;
        _Result.reserve(Pose_.size());
        for (const auto _Value : Pose_)
        {
            _Result.emplace_back(_Value);
        }
        return _Result;
    }

    ObjectMap _MakeMachineVisualSource(
        IN const iCAX::CAM::SMachineElementData& Owner_,
        IN const iCAX::CAM::SMachineVisualData& Visual_)
    {
        ObjectMap _Source;
        _Source["ownerElementID"] = Visual_.OwnerElementID;
        _Source["ownerName"] = Owner_.OriginalName.empty() ? Owner_.Name : Owner_.OriginalName;
        _Source["name"] = Visual_.Name;
        _Source["pose"] = _PoseToVariantArray(Visual_.Pose);
        _Source["geometry"] = Visual_.Geometry;
        _Source["material"] = Visual_.Material;
        _Source["castShadows"] = Visual_.bCastShadows;
        _Source["transparency"] = Visual_.dTransparency;
        return _Source;
    }

    ObjectMap _MakeMachineCollisionSource(
        IN const iCAX::CAM::SMachineElementData& Owner_,
        IN const iCAX::CAM::SMachineCollisionData& Collision_)
    {
        ObjectMap _Source;
        _Source["ownerElementID"] = Collision_.OwnerElementID;
        _Source["ownerName"] = Owner_.OriginalName.empty() ? Owner_.Name : Owner_.OriginalName;
        _Source["name"] = Collision_.Name;
        _Source["pose"] = _PoseToVariantArray(Collision_.Pose);
        _Source["geometry"] = Collision_.Geometry;
        return _Source;
    }

    void _CreateMachineStructureEntitiesFromDescription(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const iCAX::Data::uuid& MachineID_,
        IN const std::string& strMachineResourceID_,
        IN const std::string& strMachineName_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_)
    {
        auto& _Repository = Scene_.Database();
        _DeleteMachineElementEntities(_Repository, MachineID_);
        _ClearTransformChildren(_GetOrAddEntityComponent<iCAX::Transform::CTransformComponent>(_Repository.GetEntity(MachineID_)));

        // The neutral machine description has exactly one tree:
        // Element.ParentElementID defines structure, Element.JointToParent defines the edge constraint,
        // and Visual/Collision lists are attachments on the same element.
        const auto _ElementEntities = _CreateMachinePartEntities(_Repository, MachineID_, Description_.Elements);
        _CreateMachineJointComponents(_Repository, MachineID_, _ElementEntities, Description_.Elements);

        size_t _VisualIndex = 0;
        size_t _CollisionIndex = 0;
        const bool _bRenderCollisions = CountMachineVisuals(Description_) == 0;
        for (const auto& _Element : Description_.Elements)
        {
            if (!_IsMachinePartElement(_Element))
            {
                continue;
            }
            _AttachMachineElementGeometry(
                Scene_,
                MachineID_,
                strMachineResourceID_,
                strMachineName_,
                Description_,
                _ElementEntities,
                _Element,
                _VisualIndex,
                _CollisionIndex,
                _bRenderCollisions);
        }
    }

    std::string _StoreMachineDefinitionResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strResourceID_,
        IN const std::string& strMachineName_,
        IN const std::string& strSourcePath_,
        IN const iCAX::CAM::CMachineDescriptionResource& Description_)
    {
        if (strResourceID_.empty())
        {
            throw std::invalid_argument("Store CAM machine definition requires resource id");
        }

        auto& _Resources = Scene_.Resources();
        auto _pDocument = std::make_shared<iCAX::CAM::CMachineDescriptionResource>(Description_);
        auto _Info = _MakeResourceInfo(
            strResourceID_,
            strMachineName_.empty() ? std::string("Machine Description") : strMachineName_ + " Description",
            "machine.description",
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _NextResourceVersion(_Resources, strResourceID_));
        _Info.Source = strSourcePath_;
        _Info.Metadata["format"] = Description_.SourceFormat;
        _Info.Metadata["formatVersion"] = Description_.SourceFormatVersion;
        _Info.Metadata["modelName"] = Description_.ModelName;
        _Info.Metadata["linkCount"] = std::to_string(iCAX::CAM::CountMachinePartElements(Description_));
        _Info.Metadata["jointCount"] = std::to_string(iCAX::CAM::CountMachineJoints(Description_));
        _Info.Metadata["visualCount"] = std::to_string(iCAX::CAM::CountMachineVisuals(Description_));
        _Info.Metadata["collisionCount"] = std::to_string(iCAX::CAM::CountMachineCollisions(Description_));
        _Resources.Set<iCAX::CAM::CMachineDescriptionResource>(strResourceID_, _pDocument, _Info);
        return strResourceID_;
    }

    void _InstantiateMachineStructureFromResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_)
    {
        if (!pMachineEntity_ || !pMachine_)
        {
            throw std::invalid_argument("Instantiate CAM machine structure requires machine entity");
        }

        const auto _ResourceID = pMachine_->GetMachineResourceID();
        if (_ResourceID.empty())
        {
            throw std::invalid_argument("Instantiate CAM machine structure requires machine resource id");
        }

        auto _pDescription = Scene_.Resources().Get<iCAX::CAM::CMachineDescriptionResource>(_ResourceID);
        if (!_pDescription)
        {
            throw std::runtime_error("CAM machine description resource is not loaded: " + _ResourceID);
        }

        _CreateMachineStructureEntitiesFromDescription(Scene_, pMachineEntity_->GetID(), _ResourceID, pMachine_->GetName(), *_pDescription);
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>> _CreateMachineEntity(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto [_pEntity, _pMachine] = _CreateEntityWithComponent<iCAX::CAM::CMachineInstanceComponent>(Repository_);
        _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_WorkstationName, "默认工位");
        (void)_GetOrAddEntityComponent<iCAX::Transform::CTransformComponent>(_pEntity);
        _SetMachineDefaultKinematics(_GetOrAddMachineKinematics(_pEntity));
        (void)_GetOrAddMachineStatus(_pEntity);
        return { _pEntity, _pMachine };
    }

    std::string _CheckMachineLimitResult(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        const auto _Joints = _CollectMachineJointComponents(Repository_, MachineID_);
        if (_Joints.empty())
        {
            return "没有可检查的轴";
        }

        std::vector<std::string> _Violations;
        for (const auto& [_pEntity, _pJoint] : _Joints)
        {
            (void)_pEntity;
            if (!_pJoint)
            {
                continue;
            }
            if (_pJoint->GetPosition() < _pJoint->GetLowerLimit() || _pJoint->GetPosition() > _pJoint->GetUpperLimit())
            {
                _Violations.push_back(_pJoint->GetJointName());
            }
        }

        if (_Violations.empty())
        {
            return "轴限位检查通过";
        }

        std::string _Result = "轴越界：";
        for (size_t _Index = 0; _Index < _Violations.size(); ++_Index)
        {
            if (_Index > 0)
            {
                _Result += ", ";
            }
            _Result += _Violations[_Index];
        }
        return _Result;
    }

}

